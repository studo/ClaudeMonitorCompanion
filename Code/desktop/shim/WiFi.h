#pragma once

// A Wi-Fi interface that is always up.
//
// The desktop is reached over loopback, so there is no association, no scan
// and no access-point fallback to perform. Everything here reports a connected
// station, which is what lets net.cpp run unmodified: its supervision loop sees
// "connected" on the first tick and never tries anything else.
//
// localIP() deliberately reports the address *with* the port, e.g.
// "127.0.0.1:8765". net.cpp prints it as the settings URL, the settings page
// shows it, and the stats screen draws it — all three are more useful with the
// port than without, since on the desktop it is not 80.

#include <stdint.h>

#include "WString.h"

typedef enum {
  WL_IDLE_STATUS = 0,
  WL_NO_SSID_AVAIL = 1,
  WL_CONNECTED = 3,
  WL_DISCONNECTED = 6,
} wl_status_t;

typedef enum {
  WIFI_AUTH_OPEN = 0,
  WIFI_AUTH_WEP,
  WIFI_AUTH_WPA_PSK,
  WIFI_AUTH_WPA2_PSK,
  WIFI_AUTH_WPA_WPA2_PSK,
  WIFI_AUTH_WPA2_ENTERPRISE,
  WIFI_AUTH_WPA3_PSK,
  WIFI_AUTH_WPA2_WPA3_PSK,
} wifi_auth_mode_t;

typedef enum { WIFI_OFF = 0, WIFI_STA, WIFI_AP, WIFI_AP_STA } wifi_mode_t;
typedef enum { WIFI_FAST_SCAN = 0, WIFI_ALL_CHANNEL_SCAN } wifi_scan_method_t;
typedef enum {
  WIFI_CONNECT_AP_BY_SIGNAL = 0,
  WIFI_CONNECT_AP_BY_SECURITY
} wifi_sort_method_t;

// Held as text: the firmware only ever parses and prints addresses.
class IPAddress {
 public:
  IPAddress() : s_("0.0.0.0") {}
  explicit IPAddress(const char *s) : s_(s) {}
  bool fromString(const char *s) {
    s_ = s;
    return true;
  }
  String toString() const { return s_; }

 private:
  String s_;
};

class WiFiClass {
 public:
  bool mode(wifi_mode_t) { return true; }
  bool setSleep(bool) { return true; }
  bool setAutoReconnect(bool) { return true; }
  void setScanMethod(wifi_scan_method_t) {}
  void setSortMethod(wifi_sort_method_t) {}

  wl_status_t begin(const char *, const char * = nullptr, int32_t = 0,
                    const uint8_t * = nullptr, bool = true) {
    return WL_CONNECTED;
  }
  bool disconnect(bool = false) { return true; }
  wl_status_t status() { return WL_CONNECTED; }

  // An empty scan. Only reachable from the 'w' console command and from the
  // reconnect path, which never runs because status() is always connected.
  int16_t scanNetworks(bool = false, bool = false, bool = false,
                       uint32_t = 300) {
    return 0;
  }
  void scanDelete() {}
  String SSID(uint8_t) { return String(); }
  int32_t RSSI(uint8_t) { return 0; }
  uint8_t *BSSID(uint8_t) { return s_bssid; }
  int32_t channel(uint8_t) { return 0; }
  wifi_auth_mode_t encryptionType(uint8_t) { return WIFI_AUTH_OPEN; }

  // Loopback has no signal strength. Zero reads as "0 DBM" on the stats screen,
  // which is at least obviously not a radio.
  int8_t RSSI() { return 0; }
  int32_t channel() { return 0; }

  IPAddress localIP();
  IPAddress gatewayIP() { return IPAddress("127.0.0.1"); }
  String macAddress() { return String("00:00:00:00:00:00"); }

  bool softAP(const char *) { return false; }
  IPAddress softAPIP() { return IPAddress(); }
  bool softAPdisconnect(bool = false) { return true; }

 private:
  uint8_t s_bssid[6] = {};
};
extern WiFiClass WiFi;

// Used only by the 'r' reachability probe, a board-side network diagnostic.
// On the desktop it reports no route rather than pretending to test anything.
class WiFiClient {
 public:
  bool connect(const IPAddress &, uint16_t, int32_t = 0) { return false; }
  void stop() {}
};
