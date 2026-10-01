// The desktop's process-wide state: options, the listening socket, and the
// few hooks the shims call back into. Kept apart from main_desktop.cpp so the
// tests can link the firmware without a window.

#include <Arduino.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "desktop.h"

namespace desktop {
namespace {

Options s_options;
int s_listen_fd = -1;
char **s_argv = nullptr;
AfterRequest s_after_request;

}  // namespace

Options &options() { return s_options; }

std::string display_address() {
  const std::string host = s_options.listen == "0.0.0.0" ? "127.0.0.1" : s_options.listen;
  return host + ":" + std::to_string(s_options.port);
}

int listen_fd() { return s_listen_fd; }
void set_listen_fd(int fd) { s_listen_fd = fd; }

void set_argv(char **argv) { s_argv = argv; }

void restart() {
  Serial.println("desktop: restarting");
  fflush(stdout);
  // The listening socket and every SDL descriptor are close-on-exec, so the
  // new image starts clean and can bind the port again.
  execv("/proc/self/exe", s_argv);
  fprintf(stderr, "[E] restart failed: %s\n", strerror(errno));
  _exit(1);
}

void set_after_request(AfterRequest fn) { s_after_request = std::move(fn); }

void after_request(const std::string &method, const std::string &path, int code) {
  if (s_after_request) s_after_request(method, path, code);
}

}  // namespace desktop
