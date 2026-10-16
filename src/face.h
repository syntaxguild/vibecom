#pragma once
#include <Adafruit_GFX.h>
#include <math.h>
#include <string.h>
#include "behavior.h"

constexpr int16_t FACE_WIDTH = 240, FACE_HEIGHT = 160;
constexpr uint8_t FACE_BAND_ROWS = 8;
constexpr uint8_t FACE_PALETTE_SIZE = 5;
static_assert(FACE_HEIGHT % FACE_BAND_ROWS == 0 && FACE_HEIGHT / FACE_BAND_ROWS <= 32,
              "Changed face bands must fit a uint32_t mask");

// The face uses five simultaneous colors. Store indices, preserving exact RGB565
// colors in a per-frame palette. No heap allocation, including before setup().
class FaceCanvas : public GFXcanvas8 {
public:
  FaceCanvas() : GFXcanvas8(FACE_WIDTH, FACE_HEIGHT, false) { buffer = pixels; }
  uint16_t palette[FACE_PALETTE_SIZE] = {};
private:
  uint8_t pixels[FACE_WIDTH * FACE_HEIGHT] = {};
};

inline uint32_t changedFaceBands(const FaceCanvas &next, const FaceCanvas &previous) {
  uint32_t changed = 0;
  constexpr size_t pixels = FACE_WIDTH * FACE_BAND_ROWS;
  const bool paletteChanged = memcmp(next.palette, previous.palette, sizeof(next.palette)) != 0;
  for (uint8_t band = 0; band < FACE_HEIGHT / FACE_BAND_ROWS; ++band) {
    if (memcmp(next.getBuffer() + band * pixels, previous.getBuffer() + band * pixels,
               pixels) != 0) {
      changed |= 1UL << band;
    } else if (paletteChanged) {
      // Identical indices may still change color during an emotion transition.
      for (size_t i = band * pixels; i < (band + 1) * pixels; ++i) {
        const uint8_t index = next.getBuffer()[i];
        if (next.palette[index] != previous.palette[index]) {
          changed |= 1UL << band;
          break;
        }
      }
    }
  }
  return changed;
}

inline void expandFaceBand(const FaceCanvas &canvas, uint16_t row, uint16_t *output) {
  const uint8_t *indices = canvas.getBuffer() + row * FACE_WIDTH;
  for (size_t i = 0; i < FACE_WIDTH * FACE_BAND_ROWS; ++i) {
    output[i] = canvas.palette[indices[i]];
  }
}

struct FaceState {
  Mood mood;
  float happy, angry, love, sleep, gazeX, gazeY, openness;
};

inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

inline uint16_t mixColor(uint16_t a, uint16_t b, float amount) {
  const int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  const int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  return (static_cast<int>(ar + (br - ar) * amount) << 11) |
         (static_cast<int>(ag + (bg - ag) * amount) << 5) |
          static_cast<int>(ab + (bb - ab) * amount);
}

inline void drawHeart(FaceCanvas &canvas, int16_t x, int16_t y, int16_t r,
                      uint16_t color) {
  const int16_t offset = r * 2 / 3;
  canvas.fillCircle(x - offset, y - r / 3, r, color);
  canvas.fillCircle(x + offset, y - r / 3, r, color);
  canvas.fillTriangle(x - r - offset, y, x + r + offset, y, x, y + r * 2, color);
}

inline void drawSparkle(FaceCanvas &canvas, int16_t x, int16_t y, int16_t r,
                        uint16_t color) {
  canvas.fillTriangle(x, y - r, x - 2, y, x + 2, y, color);
  canvas.fillTriangle(x, y + r, x - 2, y, x + 2, y, color);
  canvas.fillTriangle(x - r, y, x, y - 2, x, y + 2, color);
  canvas.fillTriangle(x + r, y, x, y - 2, x, y + 2, color);
}

inline void drawEye(FaceCanvas &canvas, int16_t cx, int16_t cy, bool left,
                    float smile, float anger, float relaxed, float sleep,
                    float openness, uint16_t color) {
  const int16_t w = 62 + static_cast<int16_t>(4 * smile);
  const int16_t h = max(5, static_cast<int>(
      64 * (1 - 0.5f * sleep) * (1 - relaxed) * openness));
  const int16_t x = cx - w / 2, y = cy - h / 2;
  canvas.fillRoundRect(x, y, w, h, min(20, h / 2), 2); // Dim rim, palette index 2.
  canvas.fillRoundRect(x + 2, y + 1, w - 4, max(3, h - 2), min(18, h / 2), color);
  if (smile > 0.02f && h > 12) {
    // The lower lid relaxes during blinks, preserving a visible closing eyelid.
    canvas.fillCircle(cx, cy + 68 - static_cast<int16_t>(46 * smile * openness), 42, 0);
  }
  if (anger > 0 && h > 6) {
    const int16_t slope = min(h - 4, static_cast<int>(28 * anger));
    if (left) {
      canvas.fillTriangle(x - 1, y - 1, x + w, y - 1, x + w, y + slope, 0);
    } else {
      canvas.fillTriangle(x - 1, y - 1, x + w, y - 1, x - 1, y + slope, 0);
    }
  }
}

inline void drawFace(FaceCanvas &canvas, const FaceState &face, float seconds) {
  // The actual mood owns expression. A fading Happy/Love must NEVER smile in Angry.
  const bool angry = face.mood == Mood::Angry ||
      (face.mood != Mood::Love && face.angry > 0.02f);
  const float happy = angry ? 0 : face.happy;
  const float love = angry ? 0 : face.love;
  const float sleep = angry ? 0 : face.sleep;
  const float anger = angry ? (face.mood == Mood::Angry ? max(0.65f, face.angry) : face.angry) : 0;
  // Happy is wide and bouncy; Love keeps a softer, visibly different half lid.
  const float smile = max(happy * (1 - love), 0.56f * love);
  const float relaxed = love * (0.38f + 0.03f * sinf(seconds * 1.8f));
  uint16_t color = mixColor(rgb(115, 228, 227), rgb(255, 215, 121), happy);
  color = mixColor(color, rgb(255, 173, 202), love);
  color = mixColor(color, rgb(255, 99, 92), anger);
  color = mixColor(color, rgb(110, 122, 170), sleep);
  const uint16_t blush = mixColor(rgb(255, 161, 95), rgb(255, 111, 157), love);
  canvas.palette[0] = 0;
  canvas.palette[1] = color;
  canvas.palette[2] = mixColor(0, color, 0.45f);
  canvas.palette[3] = blush;
  canvas.palette[4] = mixColor(0, blush, 0.22f);
  color = 1; // All following primitives draw palette indices, not RGB565 values.

  const int16_t shakeX = lroundf(2 * anger * sinf(seconds * 83));
  const int16_t shakeY = lroundf(anger * sinf(seconds * 67));
  const int16_t sway = lroundf(2 * love * sinf(seconds * 2.1f));
  const int16_t tilt = lroundf(2 * love * sinf(seconds * 2.1f));
  const int16_t lift = lroundf(love * (-3 + sinf(seconds * 1.8f)));
  const int16_t offsetX = lroundf(face.gazeX * 1.5f) + shakeX + sway;
  const int16_t centerY = 65 + lroundf(face.gazeY * 1.5f + sinf(seconds * 2) +
      3 * happy * (1 - love) * sinf(seconds * 9)) + shakeY + lift;
  canvas.fillScreen(0);
  drawEye(canvas, 72 + offsetX, centerY - tilt, true, smile, anger, relaxed,
          sleep, face.openness, color);
  drawEye(canvas, 168 + offsetX, centerY + tilt, false, smile, anger, relaxed,
          sleep, face.openness, color);

  // Eyes carry expression; leave the lower center clean instead of a cartoon mouth.
  if (angry) {
    canvas.drawLine(215, 23, 225, 33, color);
    canvas.drawLine(225, 23, 215, 33, color);
    canvas.drawLine(216, 23, 226, 33, color);
    canvas.drawLine(226, 23, 216, 33, color);
  }
  if (!angry && (happy > 0.3f || love > 0.3f)) {
    canvas.fillRoundRect(28 + sway, 106 + lift, 25, 8, 4, 4);
    canvas.fillRoundRect(187 + sway, 106 + lift, 25, 8, 4, 4);
    for (int16_t x : {34, 41, 193, 200}) {
      canvas.drawLine(x + sway, 112 + lift, x + 4 + sway, 107 + lift, 3);
    }
    const int16_t floatY = lroundf(3 * ((1 - love) * sinf(seconds * 4) +
                                       love * sinf(seconds * 1.8f)));
    drawHeart(canvas, 18, 88 + floatY, 4, 3);
    drawSparkle(canvas, 222, 76 - floatY, 6, color);
  }
  if (sleep > 0.3f) {
    const int16_t floatY = 26 - static_cast<int16_t>(fmodf(seconds, 1.8f) * 8);
    canvas.drawChar(204, floatY, 'Z', color, 0, 2);
    canvas.drawChar(219, floatY - 5, 'z', color, 0, 1);
  }
}
