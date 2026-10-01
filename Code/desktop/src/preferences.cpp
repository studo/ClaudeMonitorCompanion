#include <Preferences.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <mutex>

#include "desktop.h"

namespace {

// config.cpp's store is written from both the render task (a swipe saves the
// style) and loop() (the settings page), exactly as on the board, where NVS
// does its own locking. One lock covers every namespace.
std::mutex s_mutex;

// Values are stored one per line as key, type and value separated by tabs, so
// the files stay readable when something needs checking by hand.
std::string escape(const std::string &in) {
  std::string out;
  for (char c : in) {
    if (c == '\\') out += "\\\\";
    else if (c == '\t') out += "\\t";
    else if (c == '\n') out += "\\n";
    else out += c;
  }
  return out;
}

std::string unescape(const std::string &in) {
  std::string out;
  for (size_t i = 0; i < in.size(); i++) {
    if (in[i] != '\\' || i + 1 == in.size()) {
      out += in[i];
      continue;
    }
    const char n = in[++i];
    out += n == 't' ? '\t' : n == 'n' ? '\n' : n;
  }
  return out;
}

std::string to_hex(const void *buf, size_t len) {
  static const char kHex[] = "0123456789abcdef";
  const uint8_t *p = (const uint8_t *)buf;
  std::string out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; i++) {
    out += kHex[p[i] >> 4];
    out += kHex[p[i] & 0xF];
  }
  return out;
}

int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

bool Preferences::begin(const char *name, bool read_only) {
  std::lock_guard<std::mutex> lock(s_mutex);
  ns_ = name;
  read_only_ = read_only;
  path_ = desktop::options().data_dir + "/" + ns_ + ".prefs";
  entries_.clear();

  FILE *f = fopen(path_.c_str(), "r");
  if (!f) return true;  // a namespace that has never been written is empty
  char *line = nullptr;
  size_t cap = 0;
  ssize_t n;
  while ((n = getline(&line, &cap, f)) > 0) {
    std::string s(line, (size_t)n);
    if (!s.empty() && s.back() == '\n') s.pop_back();
    const size_t t1 = s.find('\t');
    const size_t t2 = t1 == std::string::npos ? t1 : s.find('\t', t1 + 1);
    if (t2 == std::string::npos || t2 != t1 + 2) continue;  // malformed: skip it
    entries_[s.substr(0, t1)] = Entry{s[t1 + 1], unescape(s.substr(t2 + 1))};
  }
  free(line);
  fclose(f);
  return true;
}

bool Preferences::get(const char *key, char type, std::string &out) {
  std::lock_guard<std::mutex> lock(s_mutex);
  const auto it = entries_.find(key);
  if (it == entries_.end() || it->second.type != type) return false;
  out = it->second.value;
  return true;
}

size_t Preferences::put(const char *key, char type, const std::string &value,
                        size_t len) {
  if (read_only_) return 0;
  std::lock_guard<std::mutex> lock(s_mutex);
  Entry &e = entries_[key];
  if (e.type == type && e.value == value) return len;  // NVS skips no-op writes too
  e = Entry{type, value};
  save();
  return len;
}

// Write to a temporary file and rename it over the old one, so the store on
// disk is always either the previous version or the new one, never half of each.
void Preferences::save() {
  const std::string tmp = path_ + ".tmp";
  FILE *f = fopen(tmp.c_str(), "w");
  if (!f) {
    fprintf(stderr, "[E] prefs: cannot write %s\n", tmp.c_str());
    return;
  }
  for (const auto &kv : entries_) {
    fprintf(f, "%s\t%c\t%s\n", kv.first.c_str(), kv.second.type,
            escape(kv.second.value).c_str());
  }
  const bool ok = fflush(f) == 0 && fsync(fileno(f)) == 0;
  fclose(f);
  if (!ok || rename(tmp.c_str(), path_.c_str()) != 0) {
    fprintf(stderr, "[E] prefs: cannot save %s\n", path_.c_str());
  }
}

int32_t Preferences::getInt(const char *key, int32_t def) {
  std::string v;
  return get(key, 'i', v) ? (int32_t)strtol(v.c_str(), nullptr, 10) : def;
}

size_t Preferences::putInt(const char *key, int32_t value) {
  return put(key, 'i', std::to_string(value), sizeof(value));
}

uint32_t Preferences::getULong(const char *key, uint32_t def) {
  std::string v;
  return get(key, 'u', v) ? (uint32_t)strtoul(v.c_str(), nullptr, 10) : def;
}

size_t Preferences::putULong(const char *key, uint32_t value) {
  return put(key, 'u', std::to_string(value), sizeof(value));
}

uint16_t Preferences::getUShort(const char *key, uint16_t def) {
  std::string v;
  return get(key, 'h', v) ? (uint16_t)strtoul(v.c_str(), nullptr, 10) : def;
}

size_t Preferences::putUShort(const char *key, uint16_t value) {
  return put(key, 'h', std::to_string(value), sizeof(value));
}

String Preferences::getString(const char *key, const String &def) {
  std::string v;
  return get(key, 's', v) ? String(v) : def;
}

size_t Preferences::putString(const char *key, const char *value) {
  const std::string v = value ? value : "";
  return put(key, 's', v, v.size());
}

size_t Preferences::getBytes(const char *key, void *buf, size_t max_len) {
  std::string v;
  if (!get(key, 'b', v)) return 0;
  const size_t n = v.size() / 2 < max_len ? v.size() / 2 : max_len;
  uint8_t *out = (uint8_t *)buf;
  for (size_t i = 0; i < n; i++) {
    const int hi = hexval(v[i * 2]), lo = hexval(v[i * 2 + 1]);
    if (hi < 0 || lo < 0) return i;
    out[i] = (uint8_t)((hi << 4) | lo);
  }
  return n;
}

size_t Preferences::putBytes(const char *key, const void *buf, size_t len) {
  return put(key, 'b', to_hex(buf, len), len);
}
