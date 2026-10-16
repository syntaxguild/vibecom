#pragma once
#include <stdint.h>

namespace Hardware {
#ifdef VIBECOM_REFERENCE
constexpr int8_t tftCs = 27;
constexpr int neckMin = 15, neckCenter = 75, neckMax = 135, neckLimit = 150;
// Conservative initial PWM window; calibrate actual travel on the bench.
constexpr int servoPulseMin = 1000, servoPulseMax = 2000;
#ifdef VIBECOM_LARGE_SCREEN
constexpr const char *name = "Waveshare 17344 2-inch / TowerPro SG90 Digital";
constexpr const char *settingsNamespace = "vibecom2";
#else
constexpr const char *name = "Waveshare 15867 / TowerPro SG90 Digital";
constexpr const char *settingsNamespace = "vibecomref";
#endif
#else
// Original generic seven-pin ST7789 and user-calibrated positional servo.
constexpr int8_t tftCs = -1;
constexpr int neckMin = 0, neckCenter = 90, neckMax = 180, neckLimit = 180;
constexpr int servoPulseMin = 500, servoPulseMax = 2500;
constexpr const char *name = "Legacy 7-pin ST7789 / generic servo";
constexpr const char *settingsNamespace = "deskbot";
#endif
#ifdef VIBECOM_LARGE_SCREEN
constexpr int scaleNumerator = 4, scaleDenominator = 3;
constexpr int panelInitHeight = 320, tftRotation = 1;
#else
constexpr int scaleNumerator = 1, scaleDenominator = 1;
constexpr int panelInitHeight = 240, tftRotation = 0;
#endif
#if defined(VIBECOM_LARGE_SCREEN) || defined(WOKWI_ILI9341)
constexpr int panelWidth = 320;
#else
constexpr int panelWidth = 240;
#endif
constexpr int panelHeight = 240;
}
