#include "face.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>

void saveFrame(FaceCanvas &canvas, const std::string &path) {
  FILE *file = fopen(path.c_str(), "wb");
  assert(file);
  fprintf(file, "P6\n240 240\n255\n");
  for (int y = 0; y < 240; ++y) {
    for (int x = 0; x < 240; ++x) {
      const uint16_t color = y >= 40 && y < 200 ? canvas.palette[canvas.getPixel(x, y - 40)] : 0;
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
  FaceCanvas clean, stale;
  assert(clean.getBuffer() && stale.getBuffer());
  // Storage must live inside the canvas object, never in a fallible malloc block.
  const uintptr_t pixels = reinterpret_cast<uintptr_t>(clean.getBuffer());
  const uintptr_t object = reinterpret_cast<uintptr_t>(&clean);
  assert(pixels >= object && pixels + FACE_WIDTH * FACE_HEIGHT <= object + sizeof(clean));
  assert(changedFaceBands(clean, stale) == 0);
  clean.drawPixel(0, 0, 1);
  clean.drawPixel(239, 159, 1);
  assert(changedFaceBands(clean, stale) == ((1UL << 19) | 1));
  stale.drawPixel(0, 0, 1);
  stale.drawPixel(239, 159, 1);
  assert(changedFaceBands(clean, stale) == 0);
  clean.drawPixel(0, 8, 1);
  assert(changedFaceBands(clean, stale) == (1UL << 1));
  stale.drawPixel(0, 8, 1);
  clean.palette[1] = rgb(115, 228, 227);
  assert(changedFaceBands(clean, stale) == ((1UL << 19) | (1UL << 1) | 1));
  stale.palette[1] = clean.palette[1];
  clean.palette[4] = rgb(255, 0, 0); // Unused colors must not dirty black bands.
  assert(changedFaceBands(clean, stale) == 0);
  uint16_t band[FACE_WIDTH * FACE_BAND_ROWS];
  expandFaceBand(clean, 8, band);
  assert(band[0] == clean.palette[1] && band[1] == 0);
  // Incoming Angry must render identically even if Happy/Love/Sleep blends linger.
  // Sweep entry, the old 0.5 mouth threshold, steady anger, and the entire blink.
  for (float anger : {0.0f, 0.03f, 0.49f, 0.51f, 1.0f}) {
    for (int frame = 0; frame <= 120; ++frame) {
      const float openness = frame / 120.0f;
      FaceState face = {Mood::Angry, 0, anger, 0, 0, 0, 0, openness};
      drawFace(clean, face, frame * 0.031f);
      face.happy = face.love = face.sleep = 1;
      drawFace(stale, face, frame * 0.031f);
      assert(changedFaceBands(clean, stale) == 0);
      int visible = 0;
      for (int y = 20; y < 110; ++y)
        for (int x = 35; x < 110; ++x) visible += clean.getPixel(x, y) != 0;
      assert(visible > 0); // Angry eyelid must not disappear during a blink.
    }
  }
  FaceState face = {Mood::Idle, 0, 0, 0, 0, 0, 0, 1};
  for (Mood mood : {Mood::Idle, Mood::Happy, Mood::Angry, Mood::Love}) {
    face = {mood, 1, 1, 1, 1, 0, 0, 1};
    drawFace(clean, face, 0);
    for (int y = 112; y < FACE_HEIGHT; ++y)
      for (int x = 100; x <= 140; ++x) assert(clean.getPixel(x, y) == 0);
  }
  for (Action action : {Action::Greeting, Action::Wake, Action::Attention, Action::Play}) {
    for (int frame = 0; frame <= 60; ++frame) {
      const ActionPose pose = actionPose(action, frame / 60.0f);
      for (Mood mood : {Mood::Idle, Mood::Happy, Mood::Angry, Mood::Love}) {
        face = {mood, mood == Mood::Happy ? 1.0f : 0, mood == Mood::Angry ? 1.0f : 0,
                mood == Mood::Love ? 1.0f : 0, 0, pose.gazeX, pose.gazeY, pose.openness};
        drawFace(clean, face, frame / 30.0f);
        int visible = 0;
        for (int y = 0; y < FACE_HEIGHT; ++y)
          for (int x = 0; x < FACE_WIDTH; ++x) {
            const uint8_t pixel = clean.getPixel(x, y);
            assert(pixel < FACE_PALETTE_SIZE);
            visible += pixel != 0;
            if (y >= 112 && x >= 100 && x <= 140) assert(pixel == 0);
          }
        assert(visible > 0);
      }
    }
  }
  drawFace(clean, {Mood::Happy, 1, 0, 0, 0, 0, 0, 1}, 0);
  drawFace(stale, {Mood::Love, 0, 0, 1, 0, 0, 0, 1}, 0);
  // Expressions must differ in geometry as well as palette.
  assert(memcmp(clean.getBuffer(), stale.getBuffer(), FACE_WIDTH * FACE_HEIGHT) != 0);
  puts("PASS: inline frame storage, RGB565/dirty bands, 605 Angry frames, 976 event/mood frames, distinct Happy/Love, mouth-free expressions");

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
  int eventFrame = 0;
  for (Action action : {Action::Wake, Action::Greeting, Action::Attention, Action::Play}) {
    for (int i = 0; i < 48; ++i) {
      const float p = i / 47.0f;
      const ActionPose pose = actionPose(action, p);
      const bool happy = action == Action::Greeting || action == Action::Play;
      face = {happy ? Mood::Happy : Mood::Idle, happy ? 1.0f : 0, 0, 0,
              action == Action::Wake ? 1 - p : 0, pose.gazeX, pose.gazeY, pose.openness};
      drawFace(clean, face, i / 30.0f);
      saveFrame(clean, output + "/event-" + std::to_string(eventFrame++) + ".ppm");
    }
  }
}
