// The mouse-as-finger path, end to end: desktop::touch_push() → touch::poll()
// → the firmware's own gesture code in src/main.cpp → mood, face and view.
//
// The window turns mouse buttons into exactly these calls, so this covers
// everything between "the user clicked" and "the firmware reacted" without
// needing a display or a way to fake mouse input on Wayland.

#include <Arduino.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <string>
#include <thread>

#include "config.h"
#include "desktop.h"
#include "mood.h"
#include "pins.h"
#include "views.h"

void setup();
void loop();

namespace {

int s_failures = 0;

void check(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) s_failures++;
}

void settle() { delay(150); }

// A tap is a press and release in the same place, well inside TAP_MAX_MS.
void tap(int x, int y) {
  desktop::touch_push(true, x, y);
  delay(40);
  desktop::touch_push(false, x, y);
  settle();
}

// A drag in small steps, as a mouse produces it, then a release.
void drag(int x0, int y0, int x1, int y1) {
  const int steps = 12;
  desktop::touch_push(true, x0, y0);
  delay(10);
  for (int i = 1; i <= steps; i++) {
    desktop::touch_push(true, x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps);
    delay(10);
  }
  desktop::touch_push(false, x1, y1);
  settle();
}

}  // namespace

int main() {
  setvbuf(stdout, nullptr, _IOLBF, 0);
  char dir[] = "/tmp/cmc-touch-test-XXXXXX";
  if (!mkdtemp(dir)) return 2;
  desktop::options().data_dir = dir;
  desktop::options().mute = true;

  std::thread([] {
    setup();
    for (;;) loop();
  }).detach();
  delay(400);

  const int cx = LCD_W / 2, cy = LCD_H / 2;

  mood::post(mood::Event::NeedsInput);
  settle();
  check(mood::alerting(), "needs_input raises the alert");
  tap(cx, cy);
  check(!mood::alerting(), "a click acknowledges it");

  const int style0 = config::get("style");
  drag(60, cy, 160, cy);
  check(config::get("style") != style0, "a drag sideways changes the face style");
  drag(160, cy, 60, cy);
  check(config::get("style") == style0, "dragging back restores it");

  check(views::current() == views::View::Face, "starts on the face screen");
  drag(cx, 200, cx, 100);
  check(views::current() != views::View::Face, "a vertical drag changes the screen");
  drag(cx, 100, cx, 200);
  check(views::current() == views::View::Face, "dragging back returns to the face");

  // A drag that never reaches SWIPE_MIN_PX and is held too long to be a tap
  // does nothing at all.
  desktop::touch_push(true, cx, cy);
  delay(700);
  desktop::touch_push(false, cx + 5, cy);
  settle();
  check(views::current() == views::View::Face && config::get("style") == style0,
        "a long press is neither a tap nor a swipe");

  for (int i = 0; i < config::v::poke_count(); i++) tap(cx, cy);
  check(mood::current() == mood::Mood::Angry, "poking it repeatedly makes it angry");

  printf("%s\n", s_failures ? "FAILED" : "all passed");
  fflush(stdout);
  std::string rm = std::string("rm -rf ") + dir;
  if (system(rm.c_str()) != 0) {
  }
  _exit(s_failures ? 1 : 0);
}
