#include <Arduino.h>
#include <SPI.h>
#include "face.h"
#ifdef WOKWI_ILI9341
#include <Adafruit_ILI9341.h>
#else
#include <Adafruit_ST7789.h>
#endif
#include <ESP32Servo.h>
#include <Preferences.h>
#include <driver/gpio.h>
#include <math.h>
#include <string.h>

namespace {
// GPIO18 and GPIO19 remain dedicated to the servo and sonar, not default VSPI.
constexpr uint8_t TFT_SCLK = 14, TFT_MOSI = 23, TFT_DC = 26, TFT_RST = 33;
constexpr uint32_t TFT_SPI_HZ = 40000000; // Reduce to 20000000 for long/noisy wires.
#ifdef WOKWI_ILI9341
constexpr int8_t TFT_CS = 27;
constexpr uint8_t TFT_ROTATION = 1;
constexpr int16_t FACE_LEFT = 40, FACE_TOP = 40; // Center the face on the landscape 320x240 LCD.
#else
constexpr int8_t TFT_CS = -1; // The 7-pin ST7789 module has no exposed CS.
constexpr uint8_t TFT_ROTATION = 0;
constexpr int16_t FACE_LEFT = 0, FACE_TOP = 40;
constexpr uint8_t TFT_SPI_MODE = SPI_MODE0;
constexpr bool TFT_INVERT = true;
#endif
constexpr uint8_t SERVO_PIN = 18, TRIG_PIN = 5, ECHO_PIN = 19;
constexpr uint8_t PET_PIN = 4; // TTP223 OUT: HIGH while touched, LOW when released.
constexpr uint32_t FRAME_US = 16667, SENSOR_PERIOD_US = 80000;
constexpr uint32_t ECHO_TIMEOUT_US = 30000;
constexpr float PI_F = 3.14159265f;

enum class SonarPhase : uint8_t { Waiting, TriggerHigh, Listening };

SPIClass displaySPI(VSPI);
#ifdef WOKWI_ILI9341
Adafruit_ILI9341 display(&displaySPI, TFT_DC, TFT_CS, TFT_RST);
#else
Adafruit_ST7789 display(&displaySPI, TFT_CS, TFT_DC, TFT_RST);
#endif
FaceCanvas frameA, frameB;
FaceCanvas *drawingFrame = &frameA, *sendingFrame = &frameB;
uint16_t transferPixels[FACE_WIDTH * FACE_BAND_ROWS] = {};
Servo neck;
Companion bot;
bool displayReady = false, moodChanged = true;
float gazeX = 0, gazeY = 0, gazeTargetX = 0, gazeTargetY = 0;
float happyBlend = 0, angryBlend = 0, loveBlend = 0, sleepBlend = 0;
float neckAngle = 90;
uint32_t nextGazeMs = 0;
uint32_t nextFrameUs = 0, previousFrameUs = 0, lastServoMs = 0;
uint32_t blinkStartedMs = 0, nextBlinkMs = 0;
bool blinking = false;

SonarPhase sonarPhase = SonarPhase::Waiting;
uint32_t lastTriggerUs = 0, triggerStartedUs = 0, listeningStartedUs = 0;
portMUX_TYPE echoMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool echoArmed = false, echoRose = false, echoReady = false;
volatile uint32_t echoRiseUs = 0, echoDurationUs = 0;

// Statically backed indexed canvases: no large heap allocations before setup().
// The palette stays with its immutable frame until all changed bands are sent.
bool framePending = false;
uint16_t sendRow = 0;
uint32_t dirtyBands = 0;
uint32_t maxTransferUs = 0;
uint32_t renderedGeneration = 0, queuedGeneration = 0;
uint32_t renderedCount = 0, presentedCount = 0, skippedCount = 0;
uint32_t statsStartedMs = 0, lastLogMs = 0;
float renderFps = 0, presentFps = 0;

bool due(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}
float approach(float value, float target, float rate, float dt) {
  return value + (target - value) * (1.0f - expf(-rate * dt));
}

void showSettings() {
  const Settings &s = bot.settings;
  Serial.printf("Profile=%s | sleep=%.3f s | near=%.3f far=%.3f cm | neck min/center/max=%d/%d/%d\n",
      personalityName(s.personality), s.sleepMs / 1000.0f, s.nearCm, s.farCm,
      s.neckMin, s.neckCenter, s.neckMax);
}

void loadSettings() {
  Preferences storage;
  if (!storage.begin("deskbot", true)) {
    Serial.println(F("Settings: using defaults (no saved namespace)."));
    return;
  }
  SavedSettings data = {};
  const bool loaded = storage.getBytesLength("settings") == sizeof(data) &&
      storage.getBytes("settings", data.data(), sizeof(data)) == sizeof(data) &&
      decodeSettings(data, bot.settings);
  storage.end();
  Serial.println(loaded ? F("Settings: loaded.") : F("Settings: missing/invalid, using defaults."));
}

void saveSettings() {
  Preferences storage;
  const SavedSettings data = encodeSettings(bot.settings);
  bool saved = storage.begin("deskbot", false);
  if (saved) {
    saved = storage.putBytes("settings", data.data(), sizeof(data)) == sizeof(data);
    storage.end();
  }
  Serial.println(saved ? F("Settings: saved; retained after restart.") : F("ERROR: settings save failed."));
}

void consoleCommand(const char *line) {
  if (!strcmp(line, "help")) {
    Serial.println(F("show | profile normal/affectionate/sleepy/grumpy | set KEY VALUE | save | reset"));
    Serial.println(F("KEY: sleep (3..600 seconds), near/far (cm, 5 cm gap), neck_min/neck_center/neck_max (0..180)."));
    Serial.println(F("Edits apply now; save persists. reset restores defaults in RAM; save to persist reset."));
  } else if (!strcmp(line, "show")) showSettings();
  else if (!strcmp(line, "save")) saveSettings();
  else if (!strcmp(line, "reset") || editSettings(bot.settings, line)) {
    if (!strcmp(line, "reset")) bot.settings = Settings{};
    neckAngle = bot.clampNeck(neckAngle);
    neck.write(static_cast<int>(lroundf(neckAngle)));
    moodChanged = true;
    showSettings();
  } else Serial.println(F("ERROR: invalid command/value; unchanged. Type help."));
}

void serviceConsole() {
  // NVS writes/command output may pause a frame, so do not run them during sonar capture.
  if (sonarPhase != SonarPhase::Waiting) return;
  static char line[96];
  static size_t length = 0;
  static bool overflow = false;
  for (int budget = 0; budget < 32 && Serial.available(); ++budget) {
    const char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (overflow) Serial.println(F("ERROR: command too long; discarded."));
      else if (length) { line[length] = 0; consoleCommand(line); }
      length = 0; overflow = false;
    } else if (c >= 32 && c <= 126 && !overflow) {
      if (length + 1 < sizeof(line)) line[length++] = c;
      else overflow = true;
    }
  }
}

void IRAM_ATTR echoEdge() {
  const uint32_t now = micros();
  const bool high = gpio_get_level(static_cast<gpio_num_t>(ECHO_PIN));
  portENTER_CRITICAL_ISR(&echoMux);
  if (echoArmed) {
    if (high) {
      echoRiseUs = now;
      echoRose = true;
    } else if (echoRose) {
      echoDurationUs = now - echoRiseUs;
      echoReady = true;
      echoArmed = false;
    }
  }
  portEXIT_CRITICAL_ISR(&echoMux);
}

// Returns true only during the 10 us trigger. The loop keeps polling that phase
// without entering any SPI/Serial operation; echo timing itself is interrupt driven.
bool serviceSonar(uint32_t nowUs, uint32_t nowMs) {
  if (sonarPhase == SonarPhase::TriggerHigh) {
    if (static_cast<uint32_t>(nowUs - triggerStartedUs) >= 10) {
      digitalWrite(TRIG_PIN, LOW);
      listeningStartedUs = micros();
      sonarPhase = SonarPhase::Listening;
    }
    return true;
  }
  if (sonarPhase == SonarPhase::Listening) {
    bool ready;
    uint32_t duration;
    portENTER_CRITICAL(&echoMux);
    ready = echoReady;
    duration = echoDurationUs;
    if (ready) echoReady = false;
    portEXIT_CRITICAL(&echoMux);
    if (ready) {
      const float cm = duration / 58.0f;
      bot.acceptDistance(cm >= 2 && cm <= 400 ? cm : -1, nowMs);
      sonarPhase = SonarPhase::Waiting;
    } else if (static_cast<uint32_t>(nowUs - listeningStartedUs) >= ECHO_TIMEOUT_US) {
      portENTER_CRITICAL(&echoMux);
      echoArmed = false;
      portEXIT_CRITICAL(&echoMux);
      bot.acceptDistance(-1, nowMs);
      sonarPhase = SonarPhase::Waiting;
    }
  }
  if (sonarPhase == SonarPhase::Waiting &&
      static_cast<uint32_t>(nowUs - lastTriggerUs) >= SENSOR_PERIOD_US) {
    portENTER_CRITICAL(&echoMux);
    echoReady = false;
    echoRose = false;
    echoArmed = true;
    portEXIT_CRITICAL(&echoMux);
    digitalWrite(TRIG_PIN, HIGH);
    triggerStartedUs = micros();
    lastTriggerUs = triggerStartedUs;
    sonarPhase = SonarPhase::TriggerHigh;
    return true;
  }
  return false;
}

void updateBehavior(uint32_t now) {
  const Mood previous = bot.mood;
  const bool wasSleeping = bot.sleeping;
  const Action previousAction = bot.action;
  bot.update(digitalRead(PET_PIN) == HIGH, now);
  if (bot.mood != previous) {
    blinking = false;
    nextBlinkMs = now + 2000;
  }
  if (bot.mood != previous || bot.sleeping != wasSleeping || bot.action != previousAction)
    moodChanged = true;
}

void renderFace(uint32_t nowMs, float dt) {
  happyBlend = approach(happyBlend, bot.mood == Mood::Happy ? 1 : 0, 12, dt);
  angryBlend = approach(angryBlend, bot.mood == Mood::Angry ? 1 : 0, 16, dt);
  // Ease into affection, then let it fade more slowly after the afterglow.
  loveBlend = approach(loveBlend, bot.mood == Mood::Love ? 1 : 0,
                       bot.mood == Mood::Love ? 4.0f : 2.4f, dt);
  sleepBlend = approach(sleepBlend, bot.sleeping ? 1 : 0, 3, dt);
  if (due(nowMs, nextGazeMs)) {
    gazeTargetX = random(-7, 8);
    gazeTargetY = random(-2, 3);
    nextGazeMs = nowMs + random(1000, 2400);
  }
  const bool looking = bot.mood == Mood::Idle && !bot.sleeping;
  gazeX = approach(gazeX, looking ? gazeTargetX : 0, 4, dt);
  gazeY = approach(gazeY, looking ? gazeTargetY : 0, 4, dt);
  if (!blinking && due(nowMs, nextBlinkMs)) {
    blinking = true;
    blinkStartedMs = nowMs;
  }
  float openness = 1;
  if (blinking) {
    const uint32_t age = nowMs - blinkStartedMs;
    if (age >= 190) {
      blinking = false;
      nextBlinkMs = nowMs + random(2200, 4800);
    } else {
      const float closed = sinf(PI_F * age / 190.0f);
      openness = 1 - closed * closed;
    }
  }
  if (!displayReady) return;
  const ActionPose pose = actionPose(bot.action, bot.actionProgress(nowMs));
  const FaceState face = {bot.mood, happyBlend, angryBlend, loveBlend, sleepBlend,
                          gazeX + pose.gazeX, gazeY + pose.gazeY, openness * pose.openness};
  drawFace(*drawingFrame, face, nowMs * 0.001f);
  ++renderedGeneration;
  ++renderedCount;
}

// 8 rows = 3,840 bytes, about 0.77 ms of wire time at 40 MHz.
// Only this band blocks; sonar interrupts keep timing ECHO throughout.
void serviceDisplay() {
  if (!displayReady) return;
  if (!framePending) {
    if (queuedGeneration == renderedGeneration) return;
    skippedCount += renderedGeneration - queuedGeneration - 1;
    // Compare before recycling the previous displayed canvas for new rendering.
    dirtyBands = changedFaceBands(*drawingFrame, *sendingFrame);
    FaceCanvas *available = sendingFrame;
    sendingFrame = drawingFrame;
    drawingFrame = available;
    queuedGeneration = renderedGeneration;
    sendRow = 0;
    framePending = true;
  }
  while (sendRow < FACE_HEIGHT && !(dirtyBands & (1UL << (sendRow / FACE_BAND_ROWS)))) {
    sendRow += FACE_BAND_ROWS;
  }
  if (sendRow < FACE_HEIGHT) {
    const uint32_t startedUs = micros();
    expandFaceBand(*sendingFrame, sendRow, transferPixels);
    display.startWrite();
    display.setAddrWindow(FACE_LEFT, FACE_TOP + sendRow, FACE_WIDTH, FACE_BAND_ROWS);
    display.writePixels(transferPixels, FACE_BAND_ROWS * FACE_WIDTH);
    display.endWrite();
    maxTransferUs = max(maxTransferUs, static_cast<uint32_t>(micros() - startedUs));
    sendRow += FACE_BAND_ROWS;
  }
  if (sendRow == FACE_HEIGHT) {
    framePending = false;
    ++presentedCount;
  }
}

void serviceNeck(uint32_t now) {
  if (static_cast<uint32_t>(now - lastServoMs) < 20) return;
  const float dt = min(0.1f, (now - lastServoMs) * 0.001f);
  lastServoMs = now;
  const float phase = (now - bot.moodStartedMs) * 0.001f;
  float target = bot.settings.neckCenter;
  switch (bot.mood) {
    case Mood::Idle: target += bot.sleeping ? 0 : gazeX; break;
    case Mood::Happy: target += (bot.settings.personality == Personality::Sleepy ? 12 : 24) * sinf(phase * 4.2f); break;
    case Mood::Angry: target = bot.settings.neckMax; break;
    case Mood::Love: break; // The affection blend below also eases the release.
  }
  const float petTarget = bot.settings.neckCenter + 8 + 3 * sinf(now * 0.0021f);
  // Angry takes ownership immediately; fading affection must not turn the neck back.
  if (bot.mood != Mood::Angry) target += (petTarget - target) * loveBlend;
  const ActionPose pose = actionPose(bot.action, bot.actionProgress(now));
  if (bot.mood != Mood::Angry && bot.mood != Mood::Love && bot.action != Action::None)
    target = bot.settings.neckCenter + pose.neck;
  const float rate = bot.mood == Mood::Love ? 3.5f : bot.mood == Mood::Angry ? 8 : 12;
  neckAngle = approach(neckAngle, target, rate, dt);
  neckAngle = bot.clampNeck(neckAngle);
  neck.write(static_cast<int>(lroundf(neckAngle)));
}

void serviceLog(uint32_t now) {
  const uint32_t elapsed = now - statsStartedMs;
  if (elapsed >= 1000) {
    renderFps = renderedCount * 1000.0f / elapsed;
    presentFps = presentedCount * 1000.0f / elapsed;
    renderedCount = presentedCount = 0;
    statsStartedMs = now;
  }
  if (!moodChanged && static_cast<uint32_t>(now - lastLogMs) < 500) return;
  const char *name = bot.mood == Mood::Love ? "LOVE" : bot.mood == Mood::Angry ? "ANGRY" :
      bot.mood == Mood::Happy ? "HAPPY" : bot.sleeping ? "IDLE/SLEEPY" : "IDLE/LOOK";
  char cm[20], message[240];
  if (bot.distanceCm < 0) snprintf(cm, sizeof(cm), "no echo");
  else snprintf(cm, sizeof(cm), "%.1f cm", bot.distanceCm);
  snprintf(message, sizeof(message),
      "Distance=%s | %s | Action=%s | Pet=%s | Servo=%d | FPS draw=%.1f TFT=%.1f | skipped=%lu | SPI max=%.2f ms",
      cm, name, actionName(bot.action), bot.petHeld ? "touched" : "off", static_cast<int>(lroundf(neckAngle)),
      renderFps, presentFps, static_cast<unsigned long>(skippedCount), maxTransferUs * 0.001f);
  if (Serial.availableForWrite() >= static_cast<int>(strlen(message) + 2)) {
    Serial.println(message);
    moodChanged = false;
    lastLogMs = now;
  }
}
} // namespace

void setup() {
  Serial.setTxBufferSize(1024);
  Serial.begin(115200);
  loadSettings();
  neckAngle = bot.settings.neckCenter;
  pinMode(TRIG_PIN, OUTPUT);
  digitalWrite(TRIG_PIN, LOW);
  pinMode(ECHO_PIN, INPUT);
  pinMode(PET_PIN, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(ECHO_PIN), echoEdge, CHANGE);
  randomSeed(esp_random());

  displaySPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
#ifdef WOKWI_ILI9341
  display.begin(TFT_SPI_HZ);
  display.invertDisplay(false);
#else
  display.init(240, 240, TFT_SPI_MODE);
  display.setSPISpeed(TFT_SPI_HZ);
  display.invertDisplay(TFT_INVERT);
#endif
  display.setRotation(TFT_ROTATION);
  display.fillScreen(0); // Clear once, before sonar is armed.
  displayReady = true;
  Serial.printf("Display=READY | static indexed frames=76800 bytes | free heap=%u\n", ESP.getFreeHeap());

  ESP32PWM::allocateTimer(0);
  neck.setPeriodHertz(50);
  neck.attach(SERVO_PIN, 500, 2500);
  neck.write(bot.settings.neckCenter);
  if (!neck.attached()) Serial.println(F("ERROR: servo attachment failed."));

  const uint32_t now = millis();
  bot.begin(now);
  lastServoMs = statsStartedMs = now;
  nextBlinkMs = now + 2400;
  previousFrameUs = micros();
  nextFrameUs = previousFrameUs;
  lastTriggerUs = previousFrameUs; // Let the sensor settle for 80 ms.
  Serial.println(F("Desk Companion Bot: filtered distance, gestures, personalities. Type help + Enter."));
  showSettings();
}

void loop() {
  const uint32_t nowUs = micros();
  const uint32_t nowMs = millis();
  if (serviceSonar(nowUs, nowMs)) return;
  updateBehavior(nowMs);
  serviceConsole();
  serviceNeck(nowMs);
  if (due(nowUs, nextFrameUs)) {
    const float dt = min(0.1f, (nowUs - previousFrameUs) * 0.000001f);
    previousFrameUs = nowUs;
    nextFrameUs += FRAME_US;
    if (due(nowUs, nextFrameUs)) nextFrameUs = nowUs + FRAME_US;
    renderFace(nowMs, dt);
  }
  serviceLog(nowMs);
  serviceDisplay();
  yield();
}
