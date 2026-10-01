// touch.h for the desktop: the mouse is the finger.
//
// The window turns button-down, drag and button-up into the same stream of
// raw events the CST816 driver produces — press, moves, release, in panel
// coordinates — and the firmware's own gesture code in main.cpp makes taps and
// swipes out of them. Nothing here decides what a gesture is, so the
// thresholds in tuning.h mean the same thing with a mouse as with a finger.

#include "touch.h"

#include <deque>
#include <mutex>

#include "desktop.h"
#include "pins.h"

namespace {

std::mutex s_mutex;
std::deque<touch::Event> s_events;

// A fast drag produces hundreds of motion events between two polls. The
// gesture code only needs the path, and loop() polls every 5 ms, so a bounded
// queue loses nothing that matters.
const size_t kMaxQueued = 256;

}  // namespace

namespace touch {

bool begin() { return true; }

bool poll(Event &out) {
  std::lock_guard<std::mutex> lock(s_mutex);
  if (s_events.empty()) return false;
  out = s_events.front();
  s_events.pop_front();
  return true;
}

// No controller, so no firmware version to report.
uint8_t chip_id() { return 0; }

}  // namespace touch

void desktop::touch_push(bool pressed, int x, int y) {
  touch::Event e;
  e.pressed = pressed;
  e.x = (int16_t)(x < 0 ? 0 : x > LCD_W - 1 ? LCD_W - 1 : x);
  e.y = (int16_t)(y < 0 ? 0 : y > LCD_H - 1 ? LCD_H - 1 : y);
  e.gesture = touch::Gesture::None;

  std::lock_guard<std::mutex> lock(s_mutex);
  // Keep presses and releases; drop the oldest motion if the queue is full.
  if (s_events.size() >= kMaxQueued) {
    for (auto it = s_events.begin(); it != s_events.end(); ++it) {
      if (it->pressed) {
        s_events.erase(it);
        break;
      }
    }
  }
  s_events.push_back(e);
}
