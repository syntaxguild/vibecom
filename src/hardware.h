#pragma once
#include <stdint.h>

namespace Hardware {
#ifdef VIBECOM_REFERENCE
// Reference enclosure: Waveshare 15867 + TowerPro SG90 Digital.
constexpr int8_t tftCs = 27;
constexpr int neckMin = 15, neckCenter = 75, neckMax = 135, neckLimit = 150;
// Conservative initial PWM window; calibrate actual travel on the bench.
constexpr int servoPulseMin = 1000, servoPulseMax = 2000;
constexpr const char *name = "Waveshare 15867 / TowerPro SG90 Digital";
constexpr const char *settingsNamespace = "vibecomref";
#else
// Original generic seven-pin ST7789 and user-calibrated positional servo.
constexpr int8_t tftCs = -1;
constexpr int neckMin = 0, neckCenter = 90, neckMax = 180, neckLimit = 180;
constexpr int servoPulseMin = 500, servoPulseMax = 2500;
constexpr const char *name = "Legacy 7-pin ST7789 / generic servo";
constexpr const char *settingsNamespace = "deskbot";
#endif
}
