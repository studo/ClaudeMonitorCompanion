#pragma once

// The slice of the Arduino-ESP32 core that the firmware actually uses, on top
// of the C++ standard library. This is what lets src/ compile for Linux without
// a single edit: the firmware keeps calling millis(), Serial.printf() and
// friends, and they land here instead of in the ESP32 core.
//
// Deliberately not a general Arduino emulation. If a firmware change starts
// using something new, the desktop build fails to compile, which is the right
// moment to add it.

#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <cmath>

#include "WString.h"
#include "freertos/FreeRTOS.h"
#include "pgmspace.h"

// Arduino-ESP32 2.x pulls these into the global namespace; the firmware relies
// on it for min/max/abs on mixed literals and casts.
using std::abs;
using std::max;
using std::min;

#define PI 3.1415926535897932384626433832795
#define constrain(amt, low, high) \
  ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

// Placement attributes mean nothing off the chip.
#define IRAM_ATTR

#define HIGH 1
#define LOW 0
#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05

uint32_t millis();
uint32_t micros();
void delay(uint32_t ms);

// No GPIO on a laptop. Only here so a stray call compiles; nothing in the
// sources the desktop build uses reaches them.
inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t, uint8_t) {}
inline int digitalRead(uint8_t) { return LOW; }

inline uint32_t getCpuFrequencyMhz() { return 0; }

// The desktop already has a correct clock and the user's own timezone, so
// there is nothing to start. Ignoring the board's TZ setting is deliberate: the
// laptop knows where it is better than a string typed into a settings page.
inline void configTzTime(const char *, const char *, const char * = nullptr,
                         const char * = nullptr) {}

// The ESP32 core's log macros, routed to stderr so they reach the journal.
#define log_e(fmt, ...) fprintf(stderr, "[E] " fmt "\n", ##__VA_ARGS__)
#define log_w(fmt, ...) fprintf(stderr, "[W] " fmt "\n", ##__VA_ARGS__)
#define log_i(fmt, ...) fprintf(stderr, "[I] " fmt "\n", ##__VA_ARGS__)

// USB CDC serial. Output goes to stdout; input comes from stdin and from keys
// typed into the window, so the firmware's single-key console works from both.
class HardwareSerial {
 public:
  void begin(unsigned long) {}
  explicit operator bool() const { return true; }

  int available();
  int read();

  size_t printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
  size_t print(const char *s);
  size_t print(const String &s) { return print(s.c_str()); }
  size_t println();
  size_t println(const char *s);
  size_t println(const String &s) { return println(s.c_str()); }
};
extern HardwareSerial Serial;

// Chip facts the boot banner prints. The values are the board's own, so the
// banner does not grow warnings that only make sense on hardware (it checks
// for missing PSRAM, for one).
class EspClass {
 public:
  const char *getChipModel() { return "desktop (simulated ESP32-S3)"; }
  int getChipRevision() { return 0; }
  uint32_t getFlashChipSize() { return 16u * 1024u * 1024u; }
  uint32_t getPsramSize() { return 8u * 1024u * 1024u; }
  uint32_t getFreeHeap();
  // Re-executes the program, which is the closest thing a process has to a
  // reboot. The firmware calls this after Wi-Fi settings change.
  [[noreturn]] void restart();
};
extern EspClass ESP;
