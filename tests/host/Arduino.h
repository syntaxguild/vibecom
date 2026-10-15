#pragma once
// Host-only compatibility for compiling the real Adafruit GFX rasterizer.
#include <algorithm>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <string>
using std::min;
using std::max;
using String = std::string;
class __FlashStringHelper;
#define PROGMEM
