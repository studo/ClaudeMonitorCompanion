// Claude Monitor Companion, desktop edition.
//
// The ESP32 firmware, running in a window. src/main.cpp's setup() and loop()
// run unmodified on a thread of their own, and start the render task on
// another, just as they do on the board's two cores. This file is the part the
// board does not have: a window that shows the panel, turns the mouse into the
// touch controller, and owns the things a desktop expects — a single instance,
// a clean exit, and a polite way of asking for attention.

#include <SDL3/SDL.h>
#include <Arduino.h>
#include <Preferences.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cmath>
#include <string>
#include <thread>
#include <vector>

#include "config.h"
#include "desktop.h"
#include "face.h"
#include "mood.h"
#include "pins.h"
#include "tuning.h"
#include "views.h"

// The firmware's entry points, from src/main.cpp.
void setup();
void loop();

#ifndef CMC_VERSION
#define CMC_VERSION "dev"
#endif


namespace {

using desktop::options;

volatile sig_atomic_t s_screenshot_requested = 0;

void on_sigusr1(int) { s_screenshot_requested = 1; }

void print_usage() {
  printf(
      "Usage: claude-monitor-companion [options]\n"
      "\n"
      "The Claude Monitor Companion mascot in a window. Accepts the same HTTP\n"
      "events as the ESP32 board, so the same Claude Code hooks drive it.\n"
      "\n"
      "  --port N         listen on port N (default 8765)\n"
      "  --listen ADDR    address to listen on (default 127.0.0.1)\n"
      "  --scale N        window size as a multiple of the 240x280 panel (default 2)\n"
      "  --data-dir DIR   where settings and faces live\n"
      "                   (default $XDG_CONFIG_HOME/claude-monitor-companion)\n"
      "  --mute           no sound at all\n"
      "  --notify LIST    desktop notifications for these, comma-separated:\n"
      "                   needs_input, limit, error, done, or none\n"
      "                   (default needs_input,limit)\n"
      "  --no-attention   do not mark the taskbar entry when Claude needs you\n"
      "  --screenshot F   where SIGUSR1 saves a PNG of the panel\n"
      "                   (default <data-dir>/screenshot.png)\n"
      "  --version        print the version and exit\n"
      "\n"
      "Settings are edited at http://127.0.0.1:<port>/, as on the board.\n");
}

bool parse_notify(const std::string &list, unsigned &out) {
  out = 0;
  size_t start = 0;
  while (start <= list.size()) {
    size_t end = list.find(',', start);
    if (end == std::string::npos) end = list.size();
    const std::string item = list.substr(start, end - start);
    if (item == "needs_input") out |= desktop::kNotifyNeedsInput;
    else if (item == "limit") out |= desktop::kNotifyLimit;
    else if (item == "error") out |= desktop::kNotifyError;
    else if (item == "done") out |= desktop::kNotifyDone;
    else if (item != "none" && !item.empty()) return false;
    start = end + 1;
  }
  return true;
}

// Returns -1 to carry on, otherwise an exit code.
int parse_args(int argc, char **argv) {
  desktop::Options &o = options();
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    auto value = [&](const char *what) -> const char * {
      if (i + 1 >= argc) {
        fprintf(stderr, "%s needs %s\n", a.c_str(), what);
        return nullptr;
      }
      return argv[++i];
    };
    if (a == "--help" || a == "-h") {
      print_usage();
      return 0;
    } else if (a == "--version") {
      printf("claude-monitor-companion %s\n", CMC_VERSION);
      return 0;
    } else if (a == "--port") {
      const char *v = value("a number");
      if (!v) return 2;
      o.port = atoi(v);
      if (o.port <= 0 || o.port > 65535) {
        fprintf(stderr, "bad port: %s\n", v);
        return 2;
      }
    } else if (a == "--listen") {
      const char *v = value("an IPv4 address");
      if (!v) return 2;
      o.listen = v;
    } else if (a == "--scale") {
      const char *v = value("a number");
      if (!v) return 2;
      o.scale = atoi(v);
      if (o.scale < 1 || o.scale > 8) {
        fprintf(stderr, "scale must be 1 to 8\n");
        return 2;
      }
    } else if (a == "--data-dir") {
      const char *v = value("a directory");
      if (!v) return 2;
      o.data_dir = v;
    } else if (a == "--screenshot") {
      const char *v = value("a file name");
      if (!v) return 2;
      o.screenshot = v;
    } else if (a == "--mute") {
      o.mute = true;
    } else if (a == "--no-attention") {
      o.attention = false;
    } else if (a == "--notify") {
      const char *v = value("a list");
      if (!v) return 2;
      if (!parse_notify(v, o.notify)) {
        fprintf(stderr, "--notify takes needs_input, limit, error, done or none\n");
        return 2;
      }
    } else {
      fprintf(stderr, "unknown option: %s (try --help)\n", a.c_str());
      return 2;
    }
  }
  return -1;
}

bool make_dirs(const std::string &path) {
  for (size_t i = 1; i <= path.size(); i++) {
    if (i == path.size() || path[i] == '/') {
      const std::string part = path.substr(0, i);
      if (mkdir(part.c_str(), 0700) != 0 && errno != EEXIST) return false;
    }
  }
  return true;
}

std::string default_data_dir() {
  const char *xdg = getenv("XDG_CONFIG_HOME");
  if (xdg && *xdg) return std::string(xdg) + "/claude-monitor-companion";
  const char *home = getenv("HOME");
  return std::string(home ? home : ".") + "/.config/claude-monitor-companion";
}

// Opened before the firmware starts, so a second copy finds the port taken and
// exits with a message rather than opening a window that cannot hear anything.
int open_listen_socket() {
  in_addr addr;
  if (inet_pton(AF_INET, options().listen.c_str(), &addr) != 1) {
    fprintf(stderr, "--listen needs an IPv4 address, got %s\n", options().listen.c_str());
    return -1;
  }
  const int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (fd < 0) return -1;
  const int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in sa = {};
  sa.sin_family = AF_INET;
  sa.sin_port = htons((uint16_t)options().port);
  sa.sin_addr = addr;
  if (bind(fd, (sockaddr *)&sa, sizeof(sa)) != 0 || listen(fd, 16) != 0) {
    const int err = errno;
    close(fd);
    if (err == EADDRINUSE) {
      fprintf(stderr,
              "Port %d is already in use. Is the companion already running?\n"
              "  systemctl --user status claude-monitor-companion\n",
              options().port);
    } else {
      fprintf(stderr, "cannot listen on %s: %s\n", desktop::display_address().c_str(),
              strerror(err));
    }
    return -1;
  }
  return fd;
}

// ------------------------------------------------------ desktop defaults --

// The desktop starts with one setting different from the board: failures are
// silent. On a desk the board's buzz for every failed tool call is a gentle
// cue; from laptop speakers in a quiet room it is a lot. Everything else keeps
// the board's defaults, and this one is still changed on the settings page.
void apply_desktop_defaults() {
  config::set("error_sound", 0);
  Serial.println("desktop: applied desktop defaults (errors are silent)");
}

// The firmware fingerprints its settings table and restores defaults when it
// changes, which includes the very first run. Comparing the fingerprint before
// and after setup() is how the desktop knows it is looking at fresh defaults.
uint32_t settings_schema() {
  Preferences p;
  p.begin("mascotcfg", true);
  return p.getULong("schema", 0);
}

void firmware_thread(uint32_t schema_before) {
  setup();
  if (settings_schema() != schema_before) apply_desktop_defaults();
  for (;;) loop();
}

// Typed commands from a terminal reach the firmware's serial console. When the
// companion runs as a service stdin is /dev/null and this ends at once.
void stdin_thread() {
  char buf[256];
  for (;;) {
    const ssize_t n = read(0, buf, sizeof(buf));
    if (n > 0) {
      desktop::serial_feed(buf, (size_t)n);
    } else if (n < 0 && errno == EINTR) {
      continue;
    } else {
      return;
    }
  }
}

// ------------------------------------------------------------------ window --

struct Window {
  SDL_Window *win = nullptr;
  SDL_Renderer *ren = nullptr;
  SDL_Texture *tex = nullptr;
  std::vector<uint16_t> panel = std::vector<uint16_t>(LCD_W * LCD_H);
  std::vector<uint16_t> rgb = std::vector<uint16_t>(LCD_W * LCD_H);
  uint32_t shown_generation = 0xffffffffu;
  bool dirty = true;
  bool mouse_down = false;
  bool flashing = false;
  float wheel = 0.0f;
  uint64_t wheel_at = 0;
};

// The icon is the face itself: the default eyes on the default orange, at the
// same proportions as the panel.
SDL_Surface *make_icon() {
  const int s = 64;
  SDL_Surface *icon = SDL_CreateSurface(s, s, SDL_PIXELFORMAT_RGBA8888);
  if (!icon) return nullptr;
  const SDL_PixelFormatDetails *fmt = SDL_GetPixelFormatDetails(icon->format);
  SDL_FillSurfaceRect(icon, nullptr, SDL_MapRGBA(fmt, nullptr, 0, 0, 0, 0));
  const SDL_Rect field = {8, 4, 48, 56};
  SDL_FillSurfaceRect(icon, &field, SDL_MapRGBA(fmt, nullptr, 0xF0, 0x62, 0x0C, 0xFF));
  const SDL_Rect eyes[2] = {{18, 20, 8, 20}, {38, 20, 8, 20}};
  for (const SDL_Rect &e : eyes) {
    SDL_FillSurfaceRect(icon, &e, SDL_MapRGBA(fmt, nullptr, 0, 0, 0, 0xFF));
  }
  return icon;
}

bool open_window(Window &w) {
  // The app id is what Plasma matches against the .desktop file and what KWin
  // window rules match against, so it is set before anything else.
  SDL_SetHint(SDL_HINT_APP_ID, "claude-monitor-companion");
  SDL_SetAppMetadata("Claude Monitor Companion", CMC_VERSION, "claude-monitor-companion");
  // Keys typed into the window are console commands, not text. On a convertible
  // in tablet mode, a pop-up keyboard every time the window is focused would be
  // an imposition.
  SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "0");

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    fprintf(stderr, "cannot open a window: %s\n", SDL_GetError());
    return false;
  }
  const int scale = options().scale;
  w.win = SDL_CreateWindow("Claude Monitor Companion", LCD_W * scale, LCD_H * scale,
                           SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!w.win) {
    fprintf(stderr, "cannot create window: %s\n", SDL_GetError());
    return false;
  }
  SDL_SetWindowMinimumSize(w.win, LCD_W, LCD_H);
  const float aspect = (float)LCD_W / (float)LCD_H;
  SDL_SetWindowAspectRatio(w.win, aspect, aspect);
  if (SDL_Surface *icon = make_icon()) {
    SDL_SetWindowIcon(w.win, icon);
    SDL_DestroySurface(icon);
  }

  w.ren = SDL_CreateRenderer(w.win, nullptr);
  if (!w.ren) {
    fprintf(stderr, "cannot create renderer: %s\n", SDL_GetError());
    return false;
  }
  // Letterboxed rather than integer-only: with fractional desktop scaling an
  // integer-only fit can leave a third of the window empty. Nearest-neighbour
  // keeps every panel pixel a hard-edged block either way.
  SDL_SetRenderLogicalPresentation(w.ren, LCD_W, LCD_H, SDL_LOGICAL_PRESENTATION_LETTERBOX);
  w.tex = SDL_CreateTexture(w.ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
                            LCD_W, LCD_H);
  if (!w.tex) {
    fprintf(stderr, "cannot create texture: %s\n", SDL_GetError());
    return false;
  }
  SDL_SetTextureScaleMode(w.tex, SDL_SCALEMODE_NEAREST);
  SDL_StartTextInput(w.win);
  return true;
}

// The backlight is a PWM duty, which scales light output linearly. Pixel values
// are gamma-encoded, so the equivalent change to them is the duty raised to
// 1/2.2. Without that, the sleeping face (duty 40) would be near black instead
// of dim.
uint8_t backlight_mod(uint8_t duty) {
  return (uint8_t)lroundf(255.0f * powf(duty / 255.0f, 1.0f / 2.2f));
}

void present(Window &w) {
  uint8_t duty = 0;
  w.shown_generation = desktop::frame_snapshot(w.panel.data(), duty);
  // The firmware stores pixels byte-swapped for the panel's SPI bus.
  for (size_t i = 0; i < w.panel.size(); i++) {
    const uint16_t v = w.panel[i];
    w.rgb[i] = (uint16_t)((v >> 8) | (v << 8));
  }
  SDL_UpdateTexture(w.tex, nullptr, w.rgb.data(), LCD_W * (int)sizeof(uint16_t));
  const uint8_t m = backlight_mod(duty);
  SDL_SetTextureColorMod(w.tex, m, m, m);

  SDL_SetRenderDrawColor(w.ren, 0, 0, 0, 255);
  SDL_RenderClear(w.ren);
  SDL_RenderTexture(w.ren, w.tex, nullptr, nullptr);
  SDL_RenderPresent(w.ren);
  w.dirty = false;
}

void save_screenshot(Window &w) {
  // The panel as the firmware drew it, at its own resolution, without the
  // backlight: the thing to compare against the board.
  uint8_t duty;
  desktop::frame_snapshot(w.panel.data(), duty);
  std::vector<uint16_t> px(w.panel.size());
  for (size_t i = 0; i < px.size(); i++) {
    px[i] = (uint16_t)((w.panel[i] >> 8) | (w.panel[i] << 8));
  }
  SDL_Surface *s = SDL_CreateSurfaceFrom(LCD_W, LCD_H, SDL_PIXELFORMAT_RGB565, px.data(),
                                         LCD_W * (int)sizeof(uint16_t));
  const std::string path = options().screenshot;
  if (s && SDL_SavePNG(s, path.c_str())) {
    Serial.printf("desktop: screenshot saved to %s\n", path.c_str());
  } else {
    fprintf(stderr, "[E] screenshot failed: %s\n", SDL_GetError());
  }
  SDL_DestroySurface(s);
}

// Window coordinates to panel coordinates, through the letterbox.
bool to_panel(Window &w, SDL_Event &e, int &x, int &y) {
  SDL_ConvertEventToRenderCoordinates(w.ren, &e);
  float fx = 0, fy = 0;
  if (e.type == SDL_EVENT_MOUSE_MOTION) {
    fx = e.motion.x;
    fy = e.motion.y;
  } else {
    fx = e.button.x;
    fy = e.button.y;
  }
  x = (int)floorf(fx);
  y = (int)floorf(fy);
  return x >= 0 && x < LCD_W && y >= 0 && y < LCD_H;
}

// Space or Enter taps the middle of the face — the keyboard's way of
// acknowledging an alert.
void keyboard_tap() {
  desktop::touch_push(true, LCD_W / 2, LCD_H / 2);
  desktop::touch_push(false, LCD_W / 2, LCD_H / 2);
}

void handle_event(Window &w, SDL_Event &e, bool &running) {
  int x, y;
  switch (e.type) {
    case SDL_EVENT_QUIT:
      running = false;
      break;

    case SDL_EVENT_WINDOW_EXPOSED:
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
      w.dirty = true;
      break;

    case SDL_EVENT_WINDOW_FOCUS_GAINED:
      // Looking at the window is answer enough; stop the taskbar pulsing.
      if (w.flashing) {
        SDL_FlashWindow(w.win, SDL_FLASH_CANCEL);
        w.flashing = false;
      }
      break;

    // A left button press is a finger landing; dragging is the finger moving.
    // Touchscreen laptops get the same thing, since SDL turns touches into
    // mouse events by default.
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
      if (e.button.button == SDL_BUTTON_LEFT && to_panel(w, e, x, y)) {
        w.mouse_down = true;
        desktop::touch_push(true, x, y);
      }
      break;

    case SDL_EVENT_MOUSE_MOTION:
      if (w.mouse_down) {
        to_panel(w, e, x, y);
        desktop::touch_push(true, x, y);
      }
      break;

    case SDL_EVENT_MOUSE_BUTTON_UP:
      if (e.button.button == SDL_BUTTON_LEFT && w.mouse_down) {
        to_panel(w, e, x, y);
        desktop::touch_push(false, x, y);
        w.mouse_down = false;
      }
      break;

    // The wheel steps through screens, like a vertical swipe. Touchpads send a
    // stream of small fractional steps with momentum, so they are accumulated
    // and rate-limited to one screen per flick.
    case SDL_EVENT_MOUSE_WHEEL: {
      const uint64_t now = SDL_GetTicks();
      w.wheel += e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y;
      if (fabsf(w.wheel) >= 1.0f && now - w.wheel_at > 300) {
        views::cycle(w.wheel > 0 ? -1 : 1);
        w.wheel_at = now;
        w.wheel = 0.0f;
      } else if (now - w.wheel_at <= 300) {
        w.wheel = 0.0f;
      }
      break;
    }

    // Arrows are swipes in the direction pressed. Same calls the gesture code
    // makes, so a left arrow does what a leftward swipe does.
    case SDL_EVENT_KEY_DOWN:
      switch (e.key.key) {
        case SDLK_LEFT: face::cycle_style(SWIPE_INVERT ? 1 : -1); break;
        case SDLK_RIGHT: face::cycle_style(SWIPE_INVERT ? -1 : 1); break;
        case SDLK_UP: views::cycle(-1); break;
        case SDLK_DOWN: views::cycle(1); break;
        case SDLK_SPACE:
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
          if (!e.key.repeat) keyboard_tap();
          break;
        default: break;
      }
      break;

    // Every other key is a serial console keystroke: p, n, f, 1-9, d, [, ] and
    // the rest of the list the firmware prints at boot. Text input rather than
    // key codes, so '[' works on layouts where it needs AltGr.
    case SDL_EVENT_TEXT_INPUT:
      for (const char *p = e.text.text; *p; p++) {
        if (*p > ' ' && *p < 0x7f) {
          const char line[2] = {*p, '\n'};
          desktop::serial_feed(line, 2);
        }
      }
      break;

    default:
      break;
  }
}

}  // namespace

int main(int argc, char **argv) {
  // Line-buffered even when stdout is the journal, so log lines arrive as they
  // happen rather than in 4 kB lumps.
  setvbuf(stdout, nullptr, _IOLBF, 0);
  desktop::set_argv(argv);

  const int rc = parse_args(argc, argv);
  if (rc >= 0) return rc;

  desktop::Options &o = options();
  if (o.data_dir.empty()) o.data_dir = default_data_dir();
  if (!make_dirs(o.data_dir)) {
    fprintf(stderr, "cannot create %s: %s\n", o.data_dir.c_str(), strerror(errno));
    return 1;
  }
  if (o.screenshot.empty()) o.screenshot = o.data_dir + "/screenshot.png";

  const int fd = open_listen_socket();
  if (fd < 0) return 1;
  desktop::set_listen_fd(fd);
  if (o.listen != "127.0.0.1") {
    fprintf(stderr,
            "[W] listening on %s: the settings page is open to that network, as on the board\n",
            o.listen.c_str());
  }

  Window w;
  if (!open_window(w)) return 1;
  desktop::sound_begin();

  // A factory reset from the settings page restores the board's defaults;
  // put the desktop's one difference back afterwards.
  desktop::set_after_request([](const std::string &method, const std::string &path, int code) {
    if (method == "POST" && path == "/api/reset" && code == 200) apply_desktop_defaults();
  });

  // Reading stdin from a background job would stop the whole process with
  // SIGTTIN. Ignoring it turns that into a read error, which ends the reader.
  signal(SIGTTIN, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);
  signal(SIGUSR1, on_sigusr1);

  Serial.printf("desktop: Claude Monitor Companion %s, events at http://%s/e/<event>\n",
                CMC_VERSION, desktop::display_address().c_str());
  Serial.printf("desktop: settings and data in %s\n", o.data_dir.c_str());

  std::thread(firmware_thread, settings_schema()).detach();
  std::thread(stdin_thread).detach();

  bool running = true;
  while (running) {
    SDL_Event e;
    if (SDL_WaitEventTimeout(&e, FRAME_PERIOD_MS)) {
      do {
        handle_event(w, e, running);
      } while (SDL_PollEvent(&e));
    }

    if (s_screenshot_requested) {
      s_screenshot_requested = 0;
      save_screenshot(w);
    }

    const desktop::Attention a = desktop::take_attention();
    if (a != desktop::Attention::None &&
        !(SDL_GetWindowFlags(w.win) & SDL_WINDOW_INPUT_FOCUS)) {
      const bool asks = a == desktop::Attention::NeedsInput || a == desktop::Attention::Limit;
      if (asks && o.attention && SDL_FlashWindow(w.win, SDL_FLASH_UNTIL_FOCUSED)) {
        w.flashing = true;
      }
      desktop::notify(a);
    }
    // Answered somewhere else — in the terminal, usually. The taskbar should
    // stop asking as soon as the face does.
    if (w.flashing && !mood::alerting()) {
      SDL_FlashWindow(w.win, SDL_FLASH_CANCEL);
      w.flashing = false;
    }

    if (w.dirty || desktop::frame_generation() != w.shown_generation) present(w);
  }

  // The firmware's threads run forever, as on the board. Every setting is
  // already on disk, so there is nothing to flush; leave without running
  // destructors underneath them.
  Serial.println("desktop: bye");
  fflush(stdout);
  SDL_DestroyWindow(w.win);
  _exit(0);
}
