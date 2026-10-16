#include "behavior.h"
#include <assert.h>
#include <limits>

// Run the production behavior at 5 ms ticks with the same 80 ms sonar cadence.
struct Rig {
  Companion bot;
  uint32_t now, sampleAge = 80;
  float distance = 100;
  bool touched = false;
  explicit Rig(uint32_t start = 0) : now(start) { bot.begin(now); }
  void run(uint32_t ms) {
    for (uint32_t elapsed = 0; elapsed < ms; elapsed += 5) {
      now += 5; sampleAge += 5;
      if (sampleAge >= 80) { bot.acceptDistance(distance, now); sampleAge = 0; }
      bot.update(touched, now);
      assert(validSettings(bot.settings));
    }
  }
  void tap() { touched = true; run(90); touched = false; run(90); }
};

int main() {
  assert(validSettings(Settings{}));
  assert(Settings{}.neckMax == Hardware::neckMax);
#ifdef VIBECOM_REFERENCE
  Settings legacy;
  legacy.neckMax = 180;
  Settings untouched;
  assert(!decodeSettings(encodeSettings(legacy), untouched));
  assert(untouched.neckMax == 135); // Old NVS cannot bypass the reference servo limit.
  assert(!editSettings(untouched, "set neck_max 151"));
  assert(Hardware::tftCs == 27 && Hardware::servoPulseMin == 1000);
#ifdef VIBECOM_LARGE_SCREEN
  assert(!strcmp(Hardware::settingsNamespace, "vibecom2"));
#else
  assert(!strcmp(Hardware::settingsNamespace, "vibecomref"));
#endif
#else
  assert(Hardware::tftCs == -1 && Hardware::servoPulseMin == 500);
  assert(!strcmp(Hardware::settingsNamespace, "deskbot"));
#endif
  Rig r;
  r.run(600);
  assert(r.bot.mood == Mood::Idle && !r.bot.sleeping);
  r.distance = 30; r.run(500);
  assert(r.bot.mood == Mood::Happy && r.bot.action == Action::Greeting);
  r.run(1800);
  assert(r.bot.action == Action::None);
  // One outlier must not turn a happy face angry.
  r.distance = 8; r.run(80); r.distance = 30; r.run(160);
  assert(r.bot.mood == Mood::Happy && r.bot.distanceCm == 30);
  for (int i = 0; i < 20; ++i) {
    r.distance = i % 2 ? 49.5f : 50.5f; r.run(80);
    assert(r.bot.mood == Mood::Happy);
  }
  r.distance = 54; r.run(500);
  assert(r.bot.mood == Mood::Idle);
  r.distance = 30; r.run(500);
  assert(r.bot.mood == Mood::Happy && r.bot.action == Action::None); // Greeting cooldown.
  r.distance = 10; r.run(500);
  assert(r.bot.mood == Mood::Angry);
  for (int i = 0; i < 20; ++i) {
    r.distance = i % 2 ? 14.5f : 15.5f; r.run(80);
    assert(r.bot.mood == Mood::Angry);
  }
  r.distance = 18; r.run(500);
  assert(r.bot.mood == Mood::Happy);
  r.distance = -1; r.run(160);
  assert(r.bot.mood == Mood::Happy); // Brief loss is tolerated.
  r.run(600);
  assert(r.bot.distanceCm < 0 && r.bot.mood == Mood::Idle);
  r.distance = 30; r.run(80);
  assert(r.bot.distanceCm < 0); // Fresh acquisition requires three samples.
  r.run(400);
  assert(r.bot.mood == Mood::Happy);
  r.run(8000); r.distance = 100; r.run(500); r.distance = 30; r.run(500);
  assert(r.bot.action == Action::Greeting); // Cooldown expires; next approach greets.
  Companion stalled;
  stalled.begin(0);
  for (uint32_t now : {80U, 160U, 240U}) stalled.acceptDistance(10, now);
  assert(stalled.distanceCm == 10);
  stalled.acceptDistance(100, 1000); // Even if loop servicing paused, discard stale history.
  assert(stalled.distanceCm < 0);

  Rig taps;
  taps.run(600);
  taps.touched = true; taps.run(15); taps.touched = false; taps.run(400);
  assert(!taps.bot.petHeld && taps.bot.action == Action::None); // Contact bounce.
  taps.tap(); taps.run(300);
  assert(taps.bot.action == Action::Attention);
  taps.run(1000);
  taps.tap(); taps.tap();
  assert(taps.bot.action == Action::Play && taps.bot.mood == Mood::Happy);
  taps.run(2000);
  assert(taps.bot.action == Action::None && taps.bot.mood == Mood::Idle);
  taps.distance = 10; taps.run(500);
  taps.touched = true; taps.run(800);
  assert(taps.bot.mood == Mood::Love); // Petting still wins inside personal space.
  taps.touched = false; taps.run(1150);
  assert(taps.bot.mood == Mood::Love);
  taps.run(100);
  assert(taps.bot.mood == Mood::Angry && taps.bot.action == Action::None);
  taps.tap(); taps.tap();
  assert(taps.bot.mood == Mood::Angry && taps.bot.action == Action::None);

  Rig sleep;
  sleep.run(11000);
  assert(sleep.bot.sleeping);
  sleep.touched = true; sleep.run(40);
  assert(!sleep.bot.sleeping && sleep.bot.action == Action::Wake && sleep.bot.mood == Mood::Love);
  sleep.run(1200);
  assert(sleep.bot.action == Action::None);
  Rig approach;
  approach.run(11000); approach.distance = 30; approach.run(400);
  assert(!approach.bot.sleeping && approach.bot.action == Action::Wake);
  // Stable distant readings never keep waking a sleeping companion.
  Rig distant; distant.run(30000);
  assert(distant.bot.sleeping && distant.bot.action == Action::None);

  Rig wrap(UINT32_MAX - 600);
  wrap.touched = true; wrap.run(800); wrap.touched = false; wrap.run(1150);
  assert(wrap.bot.mood == Mood::Love);
  wrap.run(150);
  assert(wrap.bot.mood == Mood::Idle);
  wrap.run(11000);
  assert(wrap.bot.sleeping);
  Rig startup;
  startup.bot.update(false, 0x90000000U);
  assert(startup.bot.sleeping); // Unsigned elapsed times work beyond signed range.

  Settings s;
  assert(editSettings(s, "set neck_min 40"));
  assert(editSettings(s, "set neck_max 140"));
  assert(editSettings(s, "set neck_center 100"));
  assert(editSettings(s, "profile affectionate"));
  assert(s.nearCm == 12 && s.farCm == 65 && s.neckMin == 40 && s.neckCenter == 100);
  assert(editSettings(s, "profile sleepy") && s.sleepMs == 6000);
  assert(editSettings(s, "profile grumpy") && s.nearCm == 22);
  assert(editSettings(s, "profile normal") && s.sleepMs == 10000);
  assert(editSettings(s, "set sleep 12.5") && s.sleepMs == 12500);
  assert(editSettings(s, "set near 15.125"));
  const SavedSettings saved = encodeSettings(s);
  for (const char *bad : {"set near nan", "set far inf", "set sleep -1", "set sleep 601",
       "set sleep 1e99", "set near 49", "set far 12", "set neck_min 180",
       "set neck_max 90", "set neck_center 0", "set neck_min 1.5", "set far 50 junk",
       "set far 50x", "profile unknown", "profile normal extra", "set unknown 3", ""}) {
    assert(!editSettings(s, bad));
    assert(encodeSettings(s) == saved);
  }
  Settings restored;
  assert(decodeSettings(saved, restored) && encodeSettings(restored) == saved);
  SavedSettings corrupt = saved; corrupt[0]++;
  assert(!decodeSettings(corrupt, restored));
  corrupt = saved; corrupt[5] = corrupt[4];
  assert(!decodeSettings(corrupt, restored));
  corrupt = saved; corrupt[7] = 256;
  assert(!decodeSettings(corrupt, restored));
  corrupt = saved; corrupt[2] = UINT32_MAX;
  assert(!decodeSettings(corrupt, restored));
  Rig calibrated; calibrated.bot.settings = s;
  assert(calibrated.bot.clampNeck(-99) == 40 && calibrated.bot.clampNeck(999) == 140);
  assert(calibrated.bot.clampNeck(100) == 100);
  // Event poses finish on center; wake opens monotonically and gestures stay bounded.
  float lastOpen = 0;
  for (int i = 0; i <= 100; ++i) {
    const float p = i / 100.0f;
    const ActionPose wake = actionPose(Action::Wake, p);
    assert(wake.openness >= lastOpen && wake.openness <= 1); lastOpen = wake.openness;
    for (Action action : {Action::Greeting, Action::Wake, Action::Attention, Action::Play}) {
      const ActionPose pose = actionPose(action, p);
      assert(fabsf(pose.neck) <= 22 && fabsf(pose.gazeX) <= 6);
      assert(calibrated.bot.clampNeck(s.neckCenter + pose.neck) >= s.neckMin);
    }
  }
  assert(fabsf(actionPose(Action::Play, 1).neck) < 0.001f);
  Companion invalid;
  invalid.begin(0);
  for (float bad : {-1.0f, 0.0f, 401.0f, std::numeric_limits<float>::quiet_NaN()})
    invalid.acceptDistance(bad, 10);
  assert(invalid.distanceCm < 0);
  puts("PASS: filtered distance/hysteresis/dropout, greeting cooldown, tap/double/hold/bounce, sleep/wake, rollover, settings validation/roundtrip, calibrated event poses");
}
