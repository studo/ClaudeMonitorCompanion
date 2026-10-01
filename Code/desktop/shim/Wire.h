#pragma once
#include <stdint.h>

// There is no I2C bus. The boot-time scan in main.cpp therefore reports an
// empty bus, which is the truth: the simulated touch panel is not on one.
class TwoWire {
 public:
  bool begin(int, int, uint32_t) { return true; }
  void beginTransmission(uint8_t) {}
  // 2 is the Arduino core's "address NACK".
  uint8_t endTransmission(bool = true) { return 2; }
};
extern TwoWire Wire;
