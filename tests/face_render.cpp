#include "face.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>

void saveFrame(GFXcanvas16 &canvas, const std::string &path) {
  FILE *file = fopen(path.c_str(), "wb");
  assert(file);
  fprintf(file, "P6\n240 240\n255\n");
  for (int y = 0; y < 240; ++y) {
    for (int x = 0; x < 240; ++x) {
      const uint16_t color = y >= 40 && y < 200 ? canvas.getPixel(x, y - 40) : 0;
      const unsigned char pixel[] = {
          static_cast<unsigned char>(((color >> 11) & 31) * 255 / 31),
          static_cast<unsigned char>(((color >> 5) & 63) * 255 / 63),
          static_cast<unsigned char>((color & 31) * 255 / 31)};
      fwrite(pixel, 1, 3, file);
    }
  }
  fclose(file);
}

int main(int argc, char **argv) {
  GFXcanvas16 clean(FACE_WIDTH, FACE_HEIGHT), stale(FACE_WIDTH, FACE_HEIGHT);
  assert(clean.getBuffer() && stale.getBuffer());
  assert(changedFaceBands(clean, stale) == 0);
  clean.drawPixel(0, 0, 1);
  clean.drawPixel(239, 159, 1);
  assert(changedFaceBands(clean, stale) == ((1UL << 19) | 1));
  stale.drawPixel(0, 0, 1);
  stale.drawPixel(239, 159, 1);
  assert(changedFaceBands(clean, stale) == 0);
  clean.drawPixel(0, 8, 1);
  assert(changedFaceBands(clean, stale) == (1UL << 1));
  // Incoming Angry must render identically even if Happy/Love/Sleep blends linger.
  // Sweep entry, the old 0.5 mouth threshold, steady anger, and the entire blink.
  for (float anger : {0.0f, 0.03f, 0.49f, 0.51f, 1.0f}) {
    for (int frame = 0; frame <= 120; ++frame) {
      const float openness = frame / 120.0f;
      FaceState face = {Mood::Angry, 0, anger, 0, 0, 0, 0, openness};
      drawFace(clean, face, frame * 0.031f);
      face.happy = face.love = face.sleep = 1;
      drawFace(stale, face, frame * 0.031f);
      assert(memcmp(clean.getBuffer(), stale.getBuffer(), FACE_WIDTH * FACE_HEIGHT * 2) == 0);
      int visible = 0;
      for (int y = 20; y < 110; ++y)
        for (int x = 35; x < 110; ++x) visible += clean.getPixel(x, y) != 0;
      assert(visible > 0); // Angry eyelid must not disappear during a blink.
    }
  }
  FaceState face = {Mood::Angry, 1, 0, 1, 0, 0, 0, 1};
  drawFace(clean, face, 0);
  assert(clean.getPixel(120, 120) != 0); // Upper crescent: frown.
  assert(clean.getPixel(120, 127) == 0); // Never the lower crescent smile.
  face = {Mood::Idle, 0, 0, 0, 0, 0, 0, 1};
  drawFace(clean, face, 0);
  assert(clean.getPixel(120, 127) != 0);
  puts("PASS: changed-band boundaries/identical frames, 605 Angry transition/blink frames, stale-affection suppression, visible eyelids, frown versus smile");

  if (argc < 2) return 0;
  const std::string output = argv[1];
  FaceState poses[] = {{Mood::Idle, 0, 0, 0, 0, 0, 0, 1},
                       {Mood::Happy, 1, 0, 0, 0, 0, 0, 1},
                       {Mood::Angry, 0, 1, 0, 0, 0, 0, 1},
                       {Mood::Love, 0, 0, 1, 0, 0, 0, 1},
                       {Mood::Idle, 0, 0, 0, 1, 0, 0, 1}};
  for (int i = 0; i < 5; ++i) {
    drawFace(clean, poses[i], 0);
    saveFrame(clean, output + "/pose-" + std::to_string(i) + ".ppm");
  }
  float affection = 0;
  for (int i = 0; i < 180; ++i) {
    const float seconds = i / 30.0f;
    const bool touching = seconds >= 0.5f && seconds < 4.2f; // Includes 1.2s afterglow.
    affection += ((touching ? 1 : 0) - affection) *
        (1 - expf(-(touching ? 4.0f : 2.4f) / 30.0f));
    float openness = 1;
    const float blinkAge = seconds - 2.7f;
    if (blinkAge >= 0 && blinkAge < 0.19f) {
      const float closed = sinf(3.14159265f * blinkAge / 0.19f);
      openness = 1 - closed * closed;
    }
    face = {touching ? Mood::Love : Mood::Idle, 0, 0, affection, 0, 0, 0, openness};
    drawFace(clean, face, seconds);
    saveFrame(clean, output + "/love-" + std::to_string(i) + ".ppm");
  }
}
