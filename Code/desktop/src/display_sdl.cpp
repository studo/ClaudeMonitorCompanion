// display.h for the desktop: the panel is a framebuffer the window presents.
//
// The firmware draws exactly as it does on the ST7789 — solid rectangles in
// pre-swapped RGB565 — and this keeps them in the same format, so what the
// window shows is pixel-for-pixel what the board's panel would hold. The
// window does the byte swap when it uploads a frame.

#include "display.h"

#include <string.h>

#include <atomic>
#include <mutex>

#include "desktop.h"
#include "pins.h"

namespace {

std::mutex s_mutex;
uint16_t s_fb[LCD_W * LCD_H];
uint8_t s_backlight = 0;  // the board's LEDC starts at zero too
std::atomic<uint32_t> s_generation{0};

}  // namespace

namespace display {

bool begin() { return true; }

void fill_rect(int x, int y, int w, int h, uint16_t color) {
  // Same clipping as the board's driver: animation maths overshoots by design.
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > LCD_W) w = LCD_W - x;
  if (y + h > LCD_H) h = LCD_H - y;
  if (w <= 0 || h <= 0) return;

  std::lock_guard<std::mutex> lock(s_mutex);
  for (int row = y; row < y + h; row++) {
    uint16_t *p = s_fb + row * LCD_W + x;
    for (int i = 0; i < w; i++) p[i] = color;
  }
  s_generation++;
}

void backlight(uint8_t duty) {
  std::lock_guard<std::mutex> lock(s_mutex);
  if (duty == s_backlight) return;
  s_backlight = duty;
  s_generation++;
}

}  // namespace display

uint32_t desktop::frame_snapshot(uint16_t *out, uint8_t &backlight) {
  std::lock_guard<std::mutex> lock(s_mutex);
  memcpy(out, s_fb, sizeof(s_fb));
  backlight = s_backlight;
  return s_generation.load();
}

uint32_t desktop::frame_generation() { return s_generation.load(); }
