#pragma once
#include <stdint.h>
#include <cmath>
#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <array>

enum class Mood : uint8_t { Idle, Happy, Angry, Love };
enum class Personality : uint8_t { Normal, Affectionate, Sleepy, Grumpy };
enum class Action : uint8_t { None, Greeting, Wake, Attention, Play };

struct ActionPose {
  float gazeX = 0, gazeY = 0, neck = 0, openness = 1;
};
inline ActionPose actionPose(Action action, float progress) {
  ActionPose pose;
  const float p = std::max(0.0f, std::min(1.0f, progress));
  const float pi = 3.14159265f, envelope = sinf(pi * p);
  switch (action) {
    case Action::Greeting:
      if (p < 0.25f) {
        pose.gazeX = -6 * sinf(pi * p * 4);
        pose.neck = pose.gazeX;
      } else {
        const float wave = (p - 0.25f) / 0.75f;
        pose.gazeY = -2 * sinf(pi * wave);
        pose.neck = 16 * sinf(4 * pi * wave) * sinf(pi * wave);
      }
      break;
    case Action::Wake:
      pose.openness = std::min(1.0f, 0.15f + 0.85f * p);
      pose.gazeY = 2 * (1 - p); pose.neck = 5 * envelope;
      break;
    case Action::Attention: pose.gazeY = -3 * envelope; pose.neck = 8 * envelope; break;
    case Action::Play:
      pose.gazeX = 6 * sinf(6 * pi * p) * envelope;
      pose.gazeY = -2 * envelope; pose.neck = 22 * sinf(6 * pi * p) * envelope;
      break;
    default: break;
  }
  return pose;
}

struct Settings {
  uint32_t sleepMs = 10000;
  float nearCm = 15, farCm = 50;
  int neckMin = 0, neckCenter = 90, neckMax = 180;
  Personality personality = Personality::Normal;
};

inline bool validSettings(const Settings &s) {
  return s.sleepMs >= 3000 && s.sleepMs <= 600000 &&
      std::isfinite(s.nearCm) && std::isfinite(s.farCm) &&
      s.nearCm >= 5 && s.nearCm <= 80 && s.farCm >= s.nearCm + 5 && s.farCm <= 200 &&
      s.neckMin >= 0 && s.neckMin < s.neckCenter &&
      s.neckCenter < s.neckMax && s.neckMax <= 180 &&
      static_cast<uint8_t>(s.personality) <= static_cast<uint8_t>(Personality::Grumpy);
}

inline const char *personalityName(Personality p) {
  switch (p) {
    case Personality::Affectionate: return "affectionate";
    case Personality::Sleepy: return "sleepy";
    case Personality::Grumpy: return "grumpy";
    default: return "normal";
  }
}
inline const char *actionName(Action a) {
  switch (a) {
    case Action::Greeting: return "greeting";
    case Action::Wake: return "wake";
    case Action::Attention: return "attention";
    case Action::Play: return "play";
    default: return "none";
  }
}

// Parse into a copy: malformed commands never partially change calibration.
inline bool editSettings(Settings &settings, const char *line) {
  char command[16], key[20], value[24], extra;
  const int fields = sscanf(line, "%15s %19s %23s %c", command, key, value, &extra);
  Settings next = settings;
  if (fields == 2 && !strcmp(command, "profile")) {
    next.sleepMs = 10000; next.nearCm = 15; next.farCm = 50;
    if (!strcmp(key, "normal")) next.personality = Personality::Normal;
    else if (!strcmp(key, "affectionate")) {
      next.personality = Personality::Affectionate; next.nearCm = 12; next.farCm = 65;
    } else if (!strcmp(key, "sleepy")) {
      next.personality = Personality::Sleepy; next.sleepMs = 6000;
    } else if (!strcmp(key, "grumpy")) {
      next.personality = Personality::Grumpy; next.nearCm = 22; next.farCm = 45;
    } else return false;
  } else if (fields == 3 && !strcmp(command, "set")) {
    char *end = nullptr;
    errno = 0;
    const double number = strtod(value, &end);
    if (errno || end == value || *end || !std::isfinite(number)) return false;
    if (!strcmp(key, "sleep")) {
      if (number < 3 || number > 600) return false;
      next.sleepMs = static_cast<uint32_t>(number * 1000);
    } else if (!strcmp(key, "near") || !strcmp(key, "far")) {
      if (number < 5 || number > 200) return false;
      if (!strcmp(key, "near")) next.nearCm = number;
      else next.farCm = number;
    } else {
      if (number < 0 || number > 180 || floor(number) != number) return false;
      if (!strcmp(key, "neck_min")) next.neckMin = number;
      else if (!strcmp(key, "neck_center")) next.neckCenter = number;
      else if (!strcmp(key, "neck_max")) next.neckMax = number;
      else return false;
    }
  } else return false;
  if (!validSettings(next)) return false;
  settings = next;
  return true;
}

// One versioned NVS value: a failed save cannot mix fields from two revisions.
using SavedSettings = std::array<uint32_t, 8>;
inline SavedSettings encodeSettings(const Settings &s) {
  return {{0x44434201, s.sleepMs, static_cast<uint32_t>(lroundf(s.nearCm * 1000)),
      static_cast<uint32_t>(lroundf(s.farCm * 1000)), static_cast<uint32_t>(s.neckMin),
      static_cast<uint32_t>(s.neckCenter), static_cast<uint32_t>(s.neckMax),
      static_cast<uint32_t>(s.personality)}};
}
inline bool decodeSettings(const SavedSettings &data, Settings &settings) {
  if (data[0] != 0x44434201 || data[4] > 180 || data[5] > 180 || data[6] > 180 || data[7] > 3)
    return false;
  Settings next;
  next.sleepMs = data[1]; next.nearCm = data[2] / 1000.0f; next.farCm = data[3] / 1000.0f;
  next.neckMin = data[4]; next.neckCenter = data[5]; next.neckMax = data[6];
  next.personality = static_cast<Personality>(data[7]);
  if (!validSettings(next)) return false;
  settings = next;
  return true;
}

class Companion {
public:
  Settings settings;
  Mood mood = Mood::Idle;
  Action action = Action::None;
  bool sleeping = false, petHeld = false;
  float distanceCm = -1;
  uint32_t moodStartedMs = 0, actionStartedMs = 0, lastInteractionMs = 0;

  void begin(uint32_t now) {
    lastInteractionMs = moodStartedMs = now;
    lastGreetingMs = now - 8000; // The first approach can greet immediately.
  }

  void acceptDistance(float cm, uint32_t now) {
    if (!std::isfinite(cm) || cm < 2 || cm > 400) return;
    if (!hasDistance || static_cast<uint32_t>(now - lastDistanceMs) >= 400) {
      sampleCount = sampleCursor = 0;
      distanceCm = movementAnchorCm = -1;
    }
    hasDistance = true;
    lastDistanceMs = now;
    samples[sampleCursor] = cm;
    sampleCursor = (sampleCursor + 1) % 3;
    // Wait for three readings before trusting a newly acquired target.
    if (sampleCount < 3) ++sampleCount;
    if (sampleCount < 3) return;
    float sorted[3] = {samples[0], samples[1], samples[2]};
    std::sort(sorted, sorted + 3);
    distanceCm = sorted[1];
    if (distanceCm <= settings.farCm || movementAnchorCm < 0 ||
        fabsf(distanceCm - movementAnchorCm) >= 4) {
      lastInteractionMs = now;
      movementAnchorCm = distanceCm;
    }
  }

  void update(bool touched, uint32_t now) {
    const bool wasSleeping = sleeping;
    if (touched != rawPet) { rawPet = touched; petEdgeMs = now; }
    if (static_cast<uint32_t>(now - petEdgeMs) >= 25 && petHeld != rawPet) {
      petHeld = rawPet;
      if (petHeld) {
        action = Action::None;
        touchStartedMs = now;
        secondTap = tapPending && static_cast<uint32_t>(now - tapReleasedMs) < 320;
        if (secondTap) tapPending = false;
      } else if (static_cast<uint32_t>(now - touchStartedMs) >= 500) {
        afterglow = true; lastPetMs = now; tapPending = false;
      } else if (secondTap) {
        startAction(Action::Play, now); secondTap = false;
      } else { tapPending = true; tapReleasedMs = now; }
    }
    if (petHeld) {
      lastInteractionMs = now;
      if (static_cast<uint32_t>(now - touchStartedMs) >= 500) tapPending = false;
    }
    if (tapPending && !petHeld && static_cast<uint32_t>(now - tapReleasedMs) >= 320) {
      tapPending = false;
      startAction(Action::Attention, now);
    }
    if (afterglow && static_cast<uint32_t>(now - lastPetMs) >= 1200) afterglow = false;
    if (action != Action::None && static_cast<uint32_t>(now - actionStartedMs) >= actionDuration())
      action = Action::None;

    if (hasDistance && static_cast<uint32_t>(now - lastDistanceMs) >= 400) {
      distanceCm = -1; sampleCount = sampleCursor = 0; movementAnchorCm = -1;
    }
    Mood desired = Mood::Idle;
    if (distanceCm >= 0) {
      if (distanceCm < settings.nearCm + (zone == Mood::Angry ? 2 : 0)) desired = Mood::Angry;
      else if (distanceCm <= settings.farCm + (zone == Mood::Happy ? 3 : 0)) desired = Mood::Happy;
    }
    if (desired != candidateZone) { candidateZone = desired; candidateStartedMs = now; }
    if (zone != desired && static_cast<uint32_t>(now - candidateStartedMs) >= 160) {
      const Mood previous = zone;
      zone = desired;
      if (zone == Mood::Happy && previous == Mood::Idle &&
          static_cast<uint32_t>(now - lastGreetingMs) >= 8000 && !petHeld && !afterglow &&
          action == Action::None && !wasSleeping) {
        startAction(Action::Greeting, now); lastGreetingMs = now;
      }
    }
    Mood next = zone;
    if (petHeld || afterglow) next = Mood::Love;
    else if (zone == Mood::Angry) action = Action::None;
    else if (action == Action::Play) next = Mood::Happy;
    if (next != mood) { mood = next; moodStartedMs = now; }
    sleeping = mood == Mood::Idle && action == Action::None &&
        static_cast<uint32_t>(now - lastInteractionMs) >= settings.sleepMs;
    if (wasSleeping && !sleeping && mood != Mood::Angry && action != Action::Play)
      startAction(Action::Wake, now);
  }

  uint32_t actionDuration() const {
    return action == Action::Play ? 1800 : action == Action::Greeting ? 1600 :
        action == Action::Wake ? 1100 : 900;
  }
  float actionProgress(uint32_t now) const {
    return action == Action::None ? 1 : std::min(1.0f,
        static_cast<uint32_t>(now - actionStartedMs) / static_cast<float>(actionDuration()));
  }
  float clampNeck(float angle) const {
    return std::max(static_cast<float>(settings.neckMin),
        std::min(static_cast<float>(settings.neckMax), angle));
  }

private:
  float samples[3] = {}, movementAnchorCm = -1;
  uint8_t sampleCount = 0, sampleCursor = 0;
  bool hasDistance = false, rawPet = false, tapPending = false, secondTap = false, afterglow = false;
  Mood zone = Mood::Idle, candidateZone = Mood::Idle;
  uint32_t lastDistanceMs = 0, candidateStartedMs = 0, petEdgeMs = 0;
  uint32_t touchStartedMs = 0, tapReleasedMs = 0, lastPetMs = 0, lastGreetingMs = 0;

  void startAction(Action next, uint32_t now) {
    action = next; actionStartedMs = lastInteractionMs = now;
  }
};
