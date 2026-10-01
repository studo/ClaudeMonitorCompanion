#pragma once

// No mDNS advertisement on the desktop: the hooks reach it on localhost, and
// .local resolution is too slow for them anyway (see Code/README.md). begin()
// reports failure so the firmware skips announcing a name nobody can resolve.
class MDNSResponder {
 public:
  bool begin(const char *) { return false; }
  void addService(const char *, const char *, int) {}
};
extern MDNSResponder MDNS;
