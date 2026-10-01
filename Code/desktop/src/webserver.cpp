#include <WebServer.h>

#include <Arduino.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include "desktop.h"

namespace {

// Generous for anything the routes accept (the largest is a custom face, 672
// hex characters) while keeping a misbehaving client from growing a buffer
// without bound.
const size_t kMaxRequest = 64 * 1024;

// A connection that has not delivered a complete request by now is dropped.
// Browsers open speculative connections they may never use; this is what stops
// those from accumulating.
const uint32_t kRequestTimeoutMs = 5000;
const size_t kMaxConns = 16;

const char *reason(int code) {
  switch (code) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 413: return "Payload Too Large";
    default:  return "Internal Server Error";
  }
}

std::string url_decode(const std::string &in) {
  std::string out;
  for (size_t i = 0; i < in.size(); i++) {
    if (in[i] == '+') {
      out += ' ';
    } else if (in[i] == '%' && i + 2 < in.size() && isxdigit((unsigned char)in[i + 1]) &&
               isxdigit((unsigned char)in[i + 2])) {
      out += (char)strtol(in.substr(i + 1, 2).c_str(), nullptr, 16);
      i += 2;
    } else {
      out += in[i];
    }
  }
  return out;
}

void parse_args(const std::string &s,
                std::vector<std::pair<std::string, std::string>> &out) {
  size_t start = 0;
  while (start < s.size()) {
    size_t end = s.find('&', start);
    if (end == std::string::npos) end = s.size();
    const std::string kv = s.substr(start, end - start);
    if (!kv.empty()) {
      const size_t eq = kv.find('=');
      if (eq == std::string::npos) out.emplace_back(url_decode(kv), "");
      else out.emplace_back(url_decode(kv.substr(0, eq)), url_decode(kv.substr(eq + 1)));
    }
    start = end + 1;
  }
}

const std::string *find_header(
    const std::vector<std::pair<std::string, std::string>> &h, const char *name) {
  for (const auto &kv : h) {
    if (strcasecmp(kv.first.c_str(), name) == 0) return &kv.second;
  }
  return nullptr;
}

// DNS rebinding works by pointing a domain name the attacker controls at
// 127.0.0.1, so a request that names a domain rather than an address is refused.
// "localhost" is the one name that cannot be rebound.
bool host_allowed(const std::string *host) {
  if (!host || host->empty()) return true;  // HTTP/1.0, or a bare tool
  std::string h = *host;
  if (h[0] == '[') return h.find(']') != std::string::npos;  // IPv6 literal
  const size_t colon = h.find(':');
  if (colon != std::string::npos) h = h.substr(0, colon);
  if (strcasecmp(h.c_str(), "localhost") == 0) return true;
  in_addr a;
  return inet_pton(AF_INET, h.c_str(), &a) == 1;
}

// Browsers attach Origin to cross-site POSTs, and to their own same-origin
// ones. Anything that names a different site than the one it is talking to is
// a page trying to drive the mascot, and is refused.
bool origin_allowed(const std::string *origin, const std::string *host,
                    const std::string *fetch_site) {
  if (fetch_site && strcasecmp(fetch_site->c_str(), "cross-site") == 0) return false;
  if (!origin) return true;
  if (!host) return false;
  return *origin == "http://" + *host;
}

}  // namespace

struct WebServer::Conn {
  int fd = -1;
  uint32_t opened_ms = 0;
  std::string in;
  bool closed = false;

  // Filled in by parse().
  std::string method, path, query, body;
  std::vector<std::pair<std::string, std::string>> headers;
};

WebServer::WebServer(int) {}

void WebServer::begin() {
  if (desktop::listen_fd() < 0) {
    Serial.println("http  : no listening socket");
  }
}

void WebServer::on(const char *uri, HTTPMethod method, THandlerFunction fn) {
  routes_.push_back(Route{uri, method, std::move(fn)});
}

void WebServer::collectHeaders(const char *keys[], size_t count) {
  wanted_headers_.clear();
  for (size_t i = 0; i < count; i++) wanted_headers_.emplace_back(keys[i]);
}

// Like the board's server, only headers asked for through collectHeaders() are
// visible to handlers. The rest are still read, for the checks above.
bool WebServer::hasHeader(const char *name) const {
  for (const std::string &w : wanted_headers_) {
    if (strcasecmp(w.c_str(), name) == 0) return find_header(headers_, name) != nullptr;
  }
  return false;
}

String WebServer::header(const char *name) const {
  if (!hasHeader(name)) return String();
  return String(*find_header(headers_, name));
}

String WebServer::argName(int i) const {
  return (i >= 0 && i < (int)args_.size()) ? String(args_[i].first) : String();
}

String WebServer::arg(int i) const {
  return (i >= 0 && i < (int)args_.size()) ? String(args_[i].second) : String();
}

String WebServer::arg(const char *name) const {
  for (const auto &kv : args_) {
    if (kv.first == name) return String(kv.second);
  }
  return String();
}

bool WebServer::hasArg(const char *name) const {
  for (const auto &kv : args_) {
    if (kv.first == name) return true;
  }
  return false;
}

void WebServer::sendHeader(const char *name, const char *value) {
  extra_headers_.emplace_back(name, value);
}

void WebServer::send(int code, const char *type, const String &body) {
  write_response(code, type, body.c_str(), body.length());
}

void WebServer::send(int code, const char *type, const char *body) {
  write_response(code, type, body, strlen(body));
}

void WebServer::send_P(int code, const char *type, const char *body) {
  write_response(code, type, body, strlen(body));
}

void WebServer::write_response(int code, const char *type, const char *body,
                               size_t len) {
  if (!current_ || responded_) return;
  responded_ = true;
  last_code_ = code;

  std::string head = "HTTP/1.1 " + std::to_string(code) + " " + reason(code) + "\r\n";
  head += "Content-Type: " + std::string(type) + "\r\n";
  head += "Content-Length: " + std::to_string(len) + "\r\n";
  head += "Connection: close\r\n";
  for (const auto &kv : extra_headers_) head += kv.first + ": " + kv.second + "\r\n";
  head += "\r\n";

  // The socket is non-blocking; the largest response is the settings page at
  // about 30 kB, which loopback swallows in one go, but wait briefly in case.
  std::string out = head;
  out.append(body, len);
  size_t sent = 0;
  while (sent < out.size()) {
    const ssize_t n = ::send(current_->fd, out.data() + sent, out.size() - sent,
                             MSG_NOSIGNAL);
    if (n > 0) {
      sent += (size_t)n;
    } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      pollfd p = {current_->fd, POLLOUT, 0};
      if (poll(&p, 1, 1000) <= 0) break;
    } else if (n < 0 && errno == EINTR) {
      continue;
    } else {
      break;
    }
  }
}

// True once a whole request has arrived.
bool WebServer::parse(Conn &c) {
  const size_t end = c.in.find("\r\n\r\n");
  if (end == std::string::npos) return false;

  c.headers.clear();
  size_t line_end = c.in.find("\r\n");
  const std::string request_line = c.in.substr(0, line_end);
  const size_t sp1 = request_line.find(' ');
  const size_t sp2 = sp1 == std::string::npos ? sp1 : request_line.find(' ', sp1 + 1);
  if (sp2 == std::string::npos) {
    c.method.clear();
    return true;  // malformed; serve() answers 400
  }
  c.method = request_line.substr(0, sp1);
  const std::string target = request_line.substr(sp1 + 1, sp2 - sp1 - 1);
  const size_t q = target.find('?');
  c.path = url_decode(target.substr(0, q));
  c.query = q == std::string::npos ? "" : target.substr(q + 1);

  size_t pos = line_end + 2;
  while (pos < end) {
    line_end = c.in.find("\r\n", pos);
    const std::string h = c.in.substr(pos, line_end - pos);
    const size_t colon = h.find(':');
    if (colon != std::string::npos) {
      std::string v = h.substr(colon + 1);
      const size_t b = v.find_first_not_of(" \t");
      v = b == std::string::npos ? "" : v.substr(b);
      c.headers.emplace_back(h.substr(0, colon), v);
    }
    pos = line_end + 2;
  }

  size_t length = 0;
  if (const std::string *cl = find_header(c.headers, "Content-Length")) {
    length = (size_t)strtoul(cl->c_str(), nullptr, 10);
  }
  if (c.in.size() < end + 4 + length) return false;
  c.body = c.in.substr(end + 4, length);
  return true;
}

void WebServer::serve(Conn &c) {
  current_ = &c;
  responded_ = false;
  last_code_ = 0;
  extra_headers_.clear();
  headers_ = c.headers;
  args_.clear();

  if (c.method.empty()) {
    write_response(400, "text/plain", "bad request\n", 12);
  } else if (!host_allowed(find_header(c.headers, "Host")) ||
             !origin_allowed(find_header(c.headers, "Origin"),
                             find_header(c.headers, "Host"),
                             find_header(c.headers, "Sec-Fetch-Site"))) {
    const char *msg = "refused: request from another site\n";
    write_response(403, "text/plain", msg, strlen(msg));
  } else {
    // Query arguments, then form fields, then the raw body as "plain" — the
    // same set the board's server exposes.
    parse_args(c.query, args_);
    const std::string *type = find_header(c.headers, "Content-Type");
    if (type && strncasecmp(type->c_str(), "application/x-www-form-urlencoded", 33) == 0) {
      parse_args(c.body, args_);
    }
    if (!c.body.empty()) args_.emplace_back("plain", c.body);

    const HTTPMethod m = c.method == "GET"    ? HTTP_GET
                         : c.method == "POST" ? HTTP_POST
                         : c.method == "HEAD" ? HTTP_HEAD
                         : c.method == "PUT"  ? HTTP_PUT
                         : c.method == "DELETE" ? HTTP_DELETE
                                                : HTTP_ANY;
    const Route *route = nullptr;
    for (const Route &r : routes_) {
      if (r.path == c.path && (r.method == HTTP_ANY || r.method == m)) {
        route = &r;
        break;
      }
    }
    if (route) route->fn();
    else if (not_found_) not_found_();
    if (!responded_) write_response(500, "text/plain", "no response\n", 12);
  }

  desktop::after_request(c.method, c.path, last_code_);
  current_ = nullptr;
  c.closed = true;
}

void WebServer::handleClient() {
  const int lfd = desktop::listen_fd();
  if (lfd < 0) return;

  for (;;) {
    const int fd = accept4(lfd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd < 0) break;
    if (conns_.size() >= kMaxConns) {
      close(fd);
      continue;
    }
    Conn *c = new Conn;
    c->fd = fd;
    c->opened_ms = millis();
    conns_.push_back(c);
  }

  const uint32_t now = millis();
  for (Conn *c : conns_) {
    char buf[4096];
    for (;;) {
      const ssize_t n = recv(c->fd, buf, sizeof(buf), 0);
      if (n > 0) {
        c->in.append(buf, (size_t)n);
        if (c->in.size() > kMaxRequest) break;
        continue;
      }
      if (n == 0) c->closed = !parse(*c);  // peer finished sending
      break;
    }
    if (c->closed) continue;
    if (c->in.size() > kMaxRequest) {
      current_ = c;
      responded_ = false;
      extra_headers_.clear();
      write_response(413, "text/plain", "too large\n", 10);
      current_ = nullptr;
      c->closed = true;
    } else if (parse(*c)) {
      serve(*c);
    } else if (now - c->opened_ms > kRequestTimeoutMs) {
      c->closed = true;
    }
  }

  for (size_t i = 0; i < conns_.size();) {
    if (conns_[i]->closed) {
      close(conns_[i]->fd);
      delete conns_[i];
      conns_[i] = conns_.back();
      conns_.pop_back();
    } else {
      i++;
    }
  }
}
