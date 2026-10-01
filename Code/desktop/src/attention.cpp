// Getting noticed on a desktop, without stealing focus.
//
// Wayland does not let a program raise itself over what you are doing, and it
// should not try. Two polite signals remain, and the window uses both for the
// cues the user asked to hear about: the taskbar entry is marked as wanting
// attention (main.cpp, via SDL_FlashWindow), and a desktop notification is
// posted here through the freedesktop interface Plasma implements.
//
// Notifications go through notify-send rather than a D-Bus library: it is part
// of every KDE and GNOME install, and a missing one costs a log line instead of
// a build dependency. The desktop-entry hint ties them to the app, so Plasma's
// System Settings → Notifications can silence or restyle them per event.

#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "desktop.h"

extern char **environ;

namespace {

std::atomic<uint8_t> s_pending{(uint8_t)desktop::Attention::None};

const char *kAppId = "claude-monitor-companion";

struct Message {
  unsigned flag;
  const char *title;
  const char *body;
  bool transient;  // gone once answered, so not worth keeping in the history
};

bool message_for(desktop::Attention a, Message &m) {
  switch (a) {
    case desktop::Attention::NeedsInput:
      m = {desktop::kNotifyNeedsInput, "Claude needs your input",
           "A permission prompt or a question is waiting in Claude Code.", true};
      return true;
    case desktop::Attention::Limit:
      m = {desktop::kNotifyLimit, "Usage limit reached",
           "Claude Code has hit its usage limit. Nothing will run until it resets.", false};
      return true;
    case desktop::Attention::Error:
      m = {desktop::kNotifyError, "Something failed",
           "A tool call or a turn in Claude Code failed.", true};
      return true;
    case desktop::Attention::Done:
      m = {desktop::kNotifyDone, "Claude finished", "The turn is done.", true};
      return true;
    default:
      return false;
  }
}

}  // namespace

void desktop::request_attention(Attention a) { s_pending.store((uint8_t)a); }

desktop::Attention desktop::take_attention() {
  return (Attention)s_pending.exchange((uint8_t)Attention::None);
}

void desktop::notify(Attention a) {
  Message m;
  if (!message_for(a, m) || !(options().notify & m.flag)) return;

  std::vector<std::string> args = {
      "notify-send",
      "--app-name=Claude Monitor Companion",
      std::string("--icon=") + kAppId,
      std::string("--hint=string:desktop-entry:") + kAppId,
      "--urgency=normal",
  };
  if (m.transient) args.push_back("--transient");
  args.push_back(m.title);
  args.push_back(m.body);

  std::vector<char *> argv;
  for (std::string &s : args) argv.push_back(&s[0]);
  argv.push_back(nullptr);

  pid_t pid;
  const int err = posix_spawnp(&pid, "notify-send", nullptr, nullptr, argv.data(), environ);
  if (err != 0) {
    static bool warned = false;
    if (!warned) {
      warned = true;
      fprintf(stderr, "[W] notify: cannot run notify-send (%s); install libnotify-bin\n",
              strerror(err));
    }
    return;
  }
  // Reap it without making anyone wait.
  std::thread([pid] {
    int status;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
  }).detach();
}
