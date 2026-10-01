#pragma once

// Arduino's String, over std::string. Only the members the firmware uses.
//
// One behaviour matters more than it looks: appending an unsigned char appends
// its decimal value, while appending a char appends the character. The JSON
// emitter in net.cpp relies on both (mood::energy() is a uint8_t, the hex digits
// are chars), so the overloads below keep them apart exactly as Arduino does.

#include <stdint.h>
#include <stdlib.h>

#include <string>

class String {
 public:
  String() = default;
  String(const char *s) : s_(s ? s : "") {}
  String(const std::string &s) : s_(s) {}
  explicit String(char c) : s_(1, c) {}
  explicit String(unsigned char v) : s_(std::to_string(v)) {}
  explicit String(int v) : s_(std::to_string(v)) {}
  explicit String(unsigned int v) : s_(std::to_string(v)) {}
  explicit String(long v) : s_(std::to_string(v)) {}
  explicit String(unsigned long v) : s_(std::to_string(v)) {}

  const char *c_str() const { return s_.c_str(); }
  unsigned int length() const { return (unsigned int)s_.size(); }
  bool isEmpty() const { return s_.empty(); }
  void reserve(unsigned int n) { s_.reserve(n); }
  const std::string &std() const { return s_; }

  char operator[](unsigned int i) const { return i < s_.size() ? s_[i] : '\0'; }

  String &operator=(const char *s) {
    s_ = s ? s : "";
    return *this;
  }

  String &operator+=(const String &o) { s_ += o.s_; return *this; }
  String &operator+=(const char *s) { if (s) s_ += s; return *this; }
  String &operator+=(char c) { s_ += c; return *this; }
  String &operator+=(unsigned char v) { s_ += std::to_string(v); return *this; }
  String &operator+=(short v) { s_ += std::to_string(v); return *this; }
  String &operator+=(unsigned short v) { s_ += std::to_string(v); return *this; }
  String &operator+=(int v) { s_ += std::to_string(v); return *this; }
  String &operator+=(unsigned int v) { s_ += std::to_string(v); return *this; }
  String &operator+=(long v) { s_ += std::to_string(v); return *this; }
  String &operator+=(unsigned long v) { s_ += std::to_string(v); return *this; }
  String &operator+=(long long v) { s_ += std::to_string(v); return *this; }
  String &operator+=(unsigned long long v) { s_ += std::to_string(v); return *this; }

  bool operator==(const String &o) const { return s_ == o.s_; }
  bool operator==(const char *s) const { return s_ == (s ? s : ""); }
  bool operator!=(const String &o) const { return !(*this == o); }
  bool operator!=(const char *s) const { return !(*this == s); }

  bool startsWith(const char *p) const { return s_.rfind(p, 0) == 0; }

  int indexOf(char c, unsigned int from = 0) const {
    const size_t i = s_.find(c, from);
    return i == std::string::npos ? -1 : (int)i;
  }

  String substring(unsigned int from) const {
    return from >= s_.size() ? String() : String(s_.substr(from));
  }
  String substring(unsigned int from, unsigned int to) const {
    if (from > to) std::swap(from, to);
    if (from >= s_.size()) return String();
    return String(s_.substr(from, to - from));
  }

  // Arduino's toInt() is atol(): leading digits, zero if there are none.
  long toInt() const { return atol(s_.c_str()); }

  void trim() {
    const char *ws = " \t\r\n\f\v";
    const size_t b = s_.find_first_not_of(ws);
    if (b == std::string::npos) {
      s_.clear();
      return;
    }
    s_ = s_.substr(b, s_.find_last_not_of(ws) - b + 1);
  }

  friend String operator+(const String &a, const String &b) {
    return String(a.s_ + b.s_);
  }
  friend String operator+(const String &a, const char *b) {
    return String(a.s_ + (b ? b : ""));
  }

 private:
  std::string s_;
};
