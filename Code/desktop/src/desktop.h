#pragma once

// State shared between the desktop's own pieces: the window (main thread), the
// shimmed Arduino runtime, and the desktop implementations of display.h,
// touch.h and bell.h. The firmware never includes this.

#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <string>

namespace desktop {

// What the user chose to be told about, beyond the face itself.
enum Notify : unsigned {
  kNotifyNeedsInput = 1u << 0,
  kNotifyLimit = 1u << 1,
  kNotifyError = 1u << 2,
  kNotifyDone = 1u << 3,
};

struct Options {
  std::string listen = "127.0.0.1";
  int port = 8765;
  int scale = 2;
  std::string data_dir;
  std::string screenshot;  // where SIGUSR1 writes a PNG of the panel
  bool mute = false;
  bool attention = true;   // flash the taskbar entry on alerts
  unsigned notify = kNotifyNeedsInput | kNotifyLimit;
};

Options &options();

// "127.0.0.1:8765" — what the firmware is told its IP is.
std::string display_address();

// The listening socket, opened by main() before the firmware starts.
int listen_fd();
void set_listen_fd(int fd);

// For ESP.restart(), which re-executes the program with its original arguments.
void set_argv(char **argv);
[[noreturn]] void restart();

// Characters for the firmware's serial console, from stdin or the window.
void serial_feed(const char *s, size_t n);

// Called after every HTTP request, once the firmware's handler has run.
using AfterRequest =
    std::function<void(const std::string &method, const std::string &path, int code)>;
void set_after_request(AfterRequest fn);
void after_request(const std::string &method, const std::string &path, int code);

// ---- display.cpp (desktop) -------------------------------------------------

// Copies the panel into `out` (LCD_W * LCD_H pixels, byte-swapped RGB565 as the
// firmware writes it) and reports the backlight duty. Returns a counter that
// changes whenever either does, so the window only uploads what changed.
uint32_t frame_snapshot(uint16_t *out, uint8_t &backlight);
uint32_t frame_generation();

// ---- touch.cpp (desktop) ---------------------------------------------------

void touch_push(bool pressed, int x, int y);

// ---- sound.cpp -------------------------------------------------------------

// Opens the audio device. Safe to call when there is none; sound then stays off.
void sound_begin();

// ---- attention.cpp ---------------------------------------------------------

enum class Attention : uint8_t { None, NeedsInput, Limit, Error, Done };

// From any thread. The window acts on it on its next pass.
void request_attention(Attention a);
Attention take_attention();

// Main thread only: flash the taskbar entry and/or post a notification.
void notify(Attention a);

}  // namespace desktop
