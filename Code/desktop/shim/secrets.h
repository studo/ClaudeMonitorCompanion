#pragma once

// The desktop build's stand-in for include/secrets.h. The board needs Wi-Fi
// credentials compiled in for its first boot; a laptop is already online, so
// there is nothing secret here and nothing to copy.
//
// The token is the compiled-in default only. Set the real one on the settings
// page (http://127.0.0.1:8765/), the same way as on the board, so the same
// generated hook config works for both.

#define WIFI_SSID ""
#define WIFI_PASS ""
#define NOTIFY_TOKEN "change-me"
#define MDNS_NAME "claude-notifier"
