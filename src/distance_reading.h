#pragma once
#include <stdint.h>

// Pololu VL53L1X RangeValid is 0. Reject optical/bus errors before filtering:
// zero or a failed range must never masquerade as a person very close by.
inline float tofDistanceCm(uint16_t millimeters, uint8_t rangeStatus, bool busOk) {
  return busOk && rangeStatus == 0 && millimeters >= 40 && millimeters <= 4000
      ? millimeters * 0.1f : -1.0f;
}
