// Getting noticed on a desktop, without stealing focus.
//
// Wayland does not let a program raise itself over what you are doing, and it
// should not try. Two polite signals remain, and both go over the session bus:
//
//  * The taskbar entry is marked urgent through the LauncherEntry interface
//    that Plasma's task manager (and docks elsewhere) implement — the same one
//    apps use for unread-count badges. It highlights the entry and nothing
//    else.
//
//    Not SDL_FlashWindow: on Wayland SDL implements it as an xdg-activation
//    request, and KWin with its default focus-stealing prevention grants it —
//    the window is raised and takes the keyboard, mid-sentence. Verified on
//    Plasma 6.
//
//  * A freedesktop notification. Sent directly rather than through notify-send
//    so its id comes back: a "Claude needs your input" notification closes
//    itself once the question is answered, instead of lingering after the fact.
//
// The bus connection lives as long as the process. Plasma resets a launcher
// entry when the connection that set it goes away, so a one-shot sender would
// have its urgency cleared the moment it exited.
//
// Everything here runs on the main thread; sd-bus connections are not shared.

#include <stdio.h>
#include <string.h>
#include <systemd/sd-bus.h>

#include <atomic>

#include "desktop.h"

namespace {

std::atomic<uint8_t> s_pending{(uint8_t)desktop::Attention::None};

const char *kAppId = "claude-monitor-companion";
const char *kLauncherUri = "application://claude-monitor-companion.desktop";
const char *kLauncherPath = "/com/canonical/unity/launcherentry/claude_monitor_companion";

sd_bus *s_bus = nullptr;
bool s_bus_failed = false;
bool s_urgent = false;

// The notification currently on screen for an alert, so the next one replaces
// it rather than stacking, and so it can be closed when the alert clears.
uint32_t s_alert_note = 0;

sd_bus *bus() {
  if (s_bus || s_bus_failed) return s_bus;
  const int r = sd_bus_open_user(&s_bus);
  if (r < 0) {
    s_bus_failed = true;
    s_bus = nullptr;
    fprintf(stderr, "[W] attention: no session bus (%s); no taskbar or notifications\n",
            strerror(-r));
  }
  return s_bus;
}

struct Message {
  unsigned flag;
  const char *title;
  const char *body;
  bool alert;  // replaced by the next alert, and closed once it is dealt with
};

bool message_for(desktop::Attention a, Message &m) {
  switch (a) {
    case desktop::Attention::NeedsInput:
      m = {desktop::kNotifyNeedsInput, "Claude needs your input",
           "A permission prompt or a question is waiting in Claude Code.", true};
      return true;
    case desktop::Attention::Limit:
      m = {desktop::kNotifyLimit, "Usage limit reached",
           "Claude Code has hit its usage limit. Nothing will run until it resets.", true};
      return true;
    case desktop::Attention::Error:
      m = {desktop::kNotifyError, "Something failed",
           "A tool call or a turn in Claude Code failed.", false};
      return true;
    case desktop::Attention::Done:
      m = {desktop::kNotifyDone, "Claude finished", "The turn is done.", false};
      return true;
    default:
      return false;
  }
}

int on_notify_reply(sd_bus_message *reply, void *userdata, sd_bus_error *) {
  const bool alert = userdata != nullptr;
  if (sd_bus_message_is_method_error(reply, nullptr)) {
    static bool warned = false;
    if (!warned) {
      warned = true;
      const sd_bus_error *e = sd_bus_message_get_error(reply);
      fprintf(stderr, "[W] attention: notification refused: %s\n", e ? e->message : "?");
    }
    return 0;
  }
  uint32_t id = 0;
  if (sd_bus_message_read(reply, "u", &id) >= 0 && alert) s_alert_note = id;
  return 0;
}

void close_alert_note() {
  if (!s_alert_note || !bus()) return;
  sd_bus_call_method_async(s_bus, nullptr, "org.freedesktop.Notifications",
                           "/org/freedesktop/Notifications",
                           "org.freedesktop.Notifications", "CloseNotification",
                           nullptr, nullptr, "u", s_alert_note);
  s_alert_note = 0;
}

}  // namespace

void desktop::request_attention(Attention a) { s_pending.store((uint8_t)a); }

desktop::Attention desktop::take_attention() {
  return (Attention)s_pending.exchange((uint8_t)Attention::None);
}

void desktop::set_urgent(bool on) {
  if (on == s_urgent || !bus()) return;
  const int r = sd_bus_emit_signal(s_bus, kLauncherPath, "com.canonical.Unity.LauncherEntry",
                                   "Update", "sa{sv}", kLauncherUri, 1, "urgent", "b",
                                   (int)on);
  if (r >= 0) s_urgent = on;
}

void desktop::notify(Attention a) {
  Message m;
  if (!message_for(a, m) || !(options().notify & m.flag) || !bus()) return;

  sd_bus_message *msg = nullptr;
  if (sd_bus_message_new_method_call(s_bus, &msg, "org.freedesktop.Notifications",
                                     "/org/freedesktop/Notifications",
                                     "org.freedesktop.Notifications", "Notify") < 0) {
    return;
  }
  // The desktop-entry hint ties the notification to the app, so Plasma's
  // System Settings > Notifications can silence or restyle it.
  const uint32_t replaces = m.alert ? s_alert_note : 0;
  sd_bus_message_append(msg, "susss", "Claude Monitor Companion", replaces, kAppId, m.title,
                        m.body);
  sd_bus_message_append(msg, "as", 0);
  sd_bus_message_open_container(msg, 'a', "{sv}");
  sd_bus_message_append(msg, "{sv}", "desktop-entry", "s", kAppId);
  sd_bus_message_append(msg, "{sv}", "urgency", "y", (uint8_t)1);
  // Done and error notices are news, not tasks: keep them out of the history.
  if (!m.alert) sd_bus_message_append(msg, "{sv}", "transient", "b", 1);
  sd_bus_message_close_container(msg);
  sd_bus_message_append(msg, "i", -1);

  sd_bus_call_async(s_bus, nullptr, msg, on_notify_reply, m.alert ? (void *)1 : nullptr,
                    2 * 1000 * 1000);
  sd_bus_message_unref(msg);
}

void desktop::attention_cleared() {
  set_urgent(false);
  close_alert_note();
}

void desktop::attention_pump() {
  if (!s_bus) return;
  while (sd_bus_process(s_bus, nullptr) > 0) {
  }
  sd_bus_flush(s_bus);
}
