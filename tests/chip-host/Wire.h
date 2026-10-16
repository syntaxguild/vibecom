#pragma once
#include <stdint.h>
#include <stddef.h>
#include <vector>
class TwoWire {
  uint8_t address = 0;
  size_t cursor = 0;
  std::vector<uint8_t> tx, rx;
public:
  void beginTransmission(uint8_t addr);
  size_t write(uint8_t value);
  uint8_t endTransmission();
  uint8_t requestFrom(uint8_t addr, uint8_t count);
  int read();
};
extern TwoWire Wire;
