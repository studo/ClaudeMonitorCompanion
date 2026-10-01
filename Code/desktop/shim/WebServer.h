#pragma once

// The subset of Arduino-ESP32's synchronous WebServer that net.cpp uses, over
// POSIX sockets.
//
// It keeps the board's execution model: handleClient() is called from loop(),
// never blocks, and runs handlers on that same thread. So the routes in
// net.cpp behave exactly as they do on the ESP32, including the order in which
// they touch firmware state.
//
// Two things differ from the board, both deliberately:
//
//  * The port. The firmware asks for 80; the desktop listens wherever the
//    command line says (127.0.0.1:8765 by default), because port 80 needs root
//    and a laptop has other uses for it. The listening socket is opened by the
//    desktop's main() before the firmware starts, so a second instance fails
//    cleanly instead of running half-started.
//
//  * Browser-facing checks. A board on a LAN is reached by IP; a desktop on
//    loopback is reachable by every web page the user opens. Requests whose
//    Host is a domain name (DNS rebinding) or whose Origin is some other site
//    (cross-site form posts) are refused before any route runs. curl and the
//    board's own settings page are unaffected.

#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "WString.h"

enum HTTPMethod { HTTP_ANY, HTTP_GET, HTTP_HEAD, HTTP_POST, HTTP_PUT, HTTP_DELETE };

class WebServer {
 public:
  typedef std::function<void()> THandlerFunction;

  explicit WebServer(int port = 80);

  void begin();
  void handleClient();

  void on(const char *uri, HTTPMethod method, THandlerFunction fn);
  void onNotFound(THandlerFunction fn) { not_found_ = std::move(fn); }

  void collectHeaders(const char *keys[], size_t count);
  bool hasHeader(const char *name) const;
  String header(const char *name) const;

  int args() const { return (int)args_.size(); }
  String argName(int i) const;
  String arg(int i) const;
  String arg(const char *name) const;
  bool hasArg(const char *name) const;

  void sendHeader(const char *name, const char *value);
  void send(int code, const char *content_type, const String &body);
  void send(int code, const char *content_type, const char *body);
  void send_P(int code, const char *content_type, const char *body);

  struct Conn;

 private:
  struct Route {
    std::string path;
    HTTPMethod method;
    THandlerFunction fn;
  };

  void serve(Conn &c);
  bool parse(Conn &c);
  void write_response(int code, const char *type, const char *body, size_t len);

  std::vector<Route> routes_;
  THandlerFunction not_found_;
  std::vector<std::string> wanted_headers_;
  std::vector<Conn *> conns_;

  // The request being handled. Only valid inside a handler, which is the only
  // place net.cpp reads it.
  std::vector<std::pair<std::string, std::string>> headers_;
  std::vector<std::pair<std::string, std::string>> args_;
  std::vector<std::pair<std::string, std::string>> extra_headers_;
  Conn *current_ = nullptr;
  bool responded_ = false;
  int last_code_ = 0;
};
