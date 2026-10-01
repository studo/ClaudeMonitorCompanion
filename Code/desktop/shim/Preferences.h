#pragma once

// NVS, as a directory of small text files.
//
// One file per namespace under the data directory (by default
// ~/.config/claude-monitor-companion/), rewritten atomically on every put, so
// a crash or a power cut can lose the last change but never corrupt the rest.
// That matches the board's guarantee: every setting persists the moment it is
// changed.
//
// Values are typed in NVS and a read with the wrong type returns the default.
// That is kept here too, so a stale file cannot be misread as something else.

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <string>

#include "WString.h"

class Preferences {
 public:
  bool begin(const char *name, bool read_only = false);
  void end() {}

  int32_t getInt(const char *key, int32_t def = 0);
  size_t putInt(const char *key, int32_t value);

  uint32_t getULong(const char *key, uint32_t def = 0);
  size_t putULong(const char *key, uint32_t value);

  uint16_t getUShort(const char *key, uint16_t def = 0);
  size_t putUShort(const char *key, uint16_t value);

  String getString(const char *key, const String &def = String());
  size_t putString(const char *key, const char *value);

  size_t getBytes(const char *key, void *buf, size_t max_len);
  size_t putBytes(const char *key, const void *buf, size_t len);

 private:
  struct Entry {
    char type;  // 'i' int32, 'u' uint32, 'h' uint16, 's' string, 'b' bytes
    std::string value;
  };

  bool get(const char *key, char type, std::string &out);
  size_t put(const char *key, char type, const std::string &value, size_t len);
  void save();

  std::string ns_;
  std::string path_;
  bool read_only_ = true;
  std::map<std::string, Entry> entries_;
};
