#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>
#include <driver/gpio.h>
#include <math.h>
#include <string.h>

namespace {
constexpr uint8_t SDA_PIN = 21, SCL_PIN = 22, OLED_ADDRESS = 0x3C;
constexpr uint8_t SERVO_PIN = 18, TRIG_PIN = 5, ECHO_PIN = 19;
constexpr uint8_t PET_PIN = 4; // TTP223 OUT: HIGH while touched, LOW when released.
constexpr uint32_t FRAME_US = 16667, SENSOR_PERIOD_US = 80000;
constexpr uint32_t ECHO_TIMEOUT_US = 30000, SLEEP_AFTER_MS = 10000;
constexpr float PI_F = 3.14159265f;
constexpr size_t FRAME_BYTES = 128 * 64 / 8;

enum class Mood : uint8_t { Idle, Happy, Angry, Love };
enum class SonarPhase : uint8_t { Waiting, TriggerHigh, Listening };

Adafruit_SSD1306 display(128, 64, &Wire, -1, 400000, 400000);
Servo neck;
Mood mood = Mood::Idle;
bool displayReady = false, sleeping = false, moodChanged = true;
float distanceCm = -1.0f, movementAnchorCm = -1.0f;
float gazeX = 0, gazeY = 0, gazeTargetX = 0, gazeTargetY = 0;
float happyBlend = 0, angryBlend = 0, loveBlend = 0, sleepBlend = 0;
float neckAngle = 90;
uint32_t lastInteractionMs = 0, nextGazeMs = 0, moodStartedMs = 0;
uint32_t nextFrameUs = 0, previousFrameUs = 0, lastServoMs = 0;
uint32_t blinkStartedMs = 0, nextBlinkMs = 0;
bool blinking = false;

// Filter the active-high touch signal; release leaves a short affectionate afterglow.
bool rawPet = false, petHeld = false, petWasTouched = false;
uint32_t petEdgeMs = 0, lastPetMs = 0;

SonarPhase sonarPhase = SonarPhase::Waiting;
uint32_t lastTriggerUs = 0, triggerStartedUs = 0, listeningStartedUs = 0;
portMUX_TYPE echoMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool echoArmed = false, echoRose = false, echoReady = false;
volatile uint32_t echoRiseUs = 0, echoDurationUs = 0;

// Immutable in-flight image + last confirmed OLED contents. No frame is modified
// during transmission. New procedural frames may supersede unsent frames.
uint8_t pendingFrame[FRAME_BYTES] = {}, displayedFrame[FRAME_BYTES] = {};
bool framePending = false;
uint8_t scanPage = 0, displayErrors = 0;
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

void acceptDistance(float cm, uint32_t nowMs) {
  distanceCm = cm;
  if (cm < 0) return;
  if (cm <= 50.0f || movementAnchorCm < 0 ||
      fabsf(cm - movementAnchorCm) >= 4.0f) {
    lastInteractionMs = nowMs;
    movementAnchorCm = cm;
  }
}

// Returns true only during the 10 us trigger. The loop keeps polling that phase
// without entering any I2C/Serial operation; echo timing itself is interrupt driven.
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
      acceptDistance(cm >= 2 && cm <= 400 ? cm : -1, nowMs);
      sonarPhase = SonarPhase::Waiting;
    } else if (static_cast<uint32_t>(nowUs - listeningStartedUs) >= ECHO_TIMEOUT_US) {
      portENTER_CRITICAL(&echoMux);
      echoArmed = false;
      portEXIT_CRITICAL(&echoMux);
      acceptDistance(-1, nowMs);
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

void servicePet(uint32_t now) {
  const bool touched = digitalRead(PET_PIN) == HIGH;
  if (touched != rawPet) {
    rawPet = touched;
    petEdgeMs = now;
  }
  if (static_cast<uint32_t>(now - petEdgeMs) >= 25) petHeld = rawPet;
  if (petHeld) {
    petWasTouched = true;
    lastPetMs = now;
    lastInteractionMs = now;
  }
}

void updateMood(uint32_t now) {
  Mood next = Mood::Idle;
  if (petHeld || (petWasTouched && static_cast<uint32_t>(now - lastPetMs) < 1200)) {
    next = Mood::Love; // Petting always wins, even inside personal space.
  } else if (distanceCm >= 0 && distanceCm < 15) {
    next = Mood::Angry;
  } else if (distanceCm >= 15 && distanceCm <= 50) {
    next = Mood::Happy;
  }
  if (next != mood) {
    mood = next;
    moodStartedMs = now;
    blinking = false;
    nextBlinkMs = now + 2000;
    moodChanged = true;
  }
  const bool nextSleeping = mood == Mood::Idle &&
      static_cast<uint32_t>(now - lastInteractionMs) >= SLEEP_AFTER_MS;
  if (nextSleeping != sleeping) moodChanged = true;
  sleeping = nextSleeping;
}

void drawHeart(int16_t cx, int16_t cy, int16_t radius) {
  const int16_t lobeOffset = max(1, radius * 2 / 3);
  display.fillCircle(cx - lobeOffset, cy - radius / 3, radius, SSD1306_WHITE);
  display.fillCircle(cx + lobeOffset, cy - radius / 3, radius, SSD1306_WHITE);
  display.fillTriangle(cx - radius - lobeOffset, cy, cx + radius + lobeOffset, cy,
                       cx, cy + radius * 2, SSD1306_WHITE);
}

void drawSparkle(int16_t x, int16_t y, int16_t r) {
  display.fillTriangle(x, y - r, x - 1, y, x + 1, y, SSD1306_WHITE);
  display.fillTriangle(x, y + r, x - 1, y, x + 1, y, SSD1306_WHITE);
  display.fillTriangle(x - r, y, x, y - 1, x, y + 1, SSD1306_WHITE);
  display.fillTriangle(x + r, y, x, y - 1, x, y + 1, SSD1306_WHITE);
}

void drawEye(int16_t cx, int16_t cy, bool left, float openness, float seconds) {
  // Petting relaxes the same capsule eyes into a soft smile, without a shape swap.
  const float smile = max(happyBlend, 0.82f * loveBlend);
  const float relaxed = loveBlend * (0.18f + 0.03f * sinf(seconds * 1.8f));
  const int16_t w = 34 + static_cast<int16_t>(2 * smile);
  const int16_t h = max(3, static_cast<int>(
      34 * (1 - 0.55f * sleepBlend) * (1 - relaxed) * openness));
  const int16_t x = cx - w / 2, y = cy - h / 2;
  display.fillRoundRect(x, y, w, h, min(12, h / 2), SSD1306_WHITE);
  if (smile > 0.02f && h > 8) {
    // Carve the lower lid out of a solid capsule, morphing into a smiling arch.
    // Relax the lower lid during blinks so the shrinking eye cannot be cut away.
    display.fillCircle(cx, cy + 36 - static_cast<int16_t>(25 * smile * openness),
                       22, SSD1306_BLACK);
  }
  if (angryBlend > 0.02f && h > 8) {
    const int16_t slope = static_cast<int16_t>(15 * angryBlend);
    if (left) {
      display.fillTriangle(x - 1, y - 1, x + w, y - 1,
                           x + w, y + slope, SSD1306_BLACK);
    } else {
      display.fillTriangle(x - 1, y - 1, x + w, y - 1,
                           x - 1, y + slope, SSD1306_BLACK);
    }
  }
}

void renderFace(uint32_t nowMs, float dt) {
  happyBlend = approach(happyBlend, mood == Mood::Happy ? 1 : 0, 12, dt);
  angryBlend = approach(angryBlend, mood == Mood::Angry ? 1 : 0, 16, dt);
  // Ease into affection, then let it fade more slowly after the afterglow.
  loveBlend = approach(loveBlend, mood == Mood::Love ? 1 : 0,
                       mood == Mood::Love ? 4.0f : 2.4f, dt);
  sleepBlend = approach(sleepBlend, sleeping ? 1 : 0, 3, dt);
  if (due(nowMs, nextGazeMs)) {
    gazeTargetX = random(-7, 8);
    gazeTargetY = random(-2, 3);
    nextGazeMs = nowMs + random(1000, 2400);
  }
  const bool looking = mood == Mood::Idle && !sleeping;
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
  const float seconds = nowMs * 0.001f;
  const int16_t shakeX = lroundf(1.5f * angryBlend * sinf(seconds * 113));
  const int16_t shakeY = lroundf(angryBlend * sinf(seconds * 89));
  const int16_t petSway = lroundf(1.5f * loveBlend * sinf(seconds * 2.1f));
  const int16_t petTilt = lroundf(1.2f * loveBlend * sinf(seconds * 2.1f));
  const int16_t petLift = lroundf(loveBlend * (-1.5f + 0.5f * sinf(seconds * 1.8f)));
  const int16_t offsetX = lroundf(gazeX) + shakeX + petSway;
  const int16_t centerY = 33 + lroundf(gazeY + sinf(seconds * 2) +
      1.5f * happyBlend * sinf(seconds * 9)) + shakeY + petLift;
  if (!displayReady) return;
  display.clearDisplay();
  drawEye(39 + offsetX, centerY - petTilt, true, openness, seconds);
  drawEye(89 + offsetX, centerY + petTilt, false, openness, seconds);
  // A small smile; accent marks sit outside the eyes rather than obscuring them.
  if (angryBlend < 0.5f) {
    display.fillCircle(64 + petSway, 52 + petLift, 4, SSD1306_WHITE);
    display.fillCircle(64 + petSway, 50 + petLift, 4, SSD1306_BLACK);
  } else {
    display.fillRoundRect(60 + shakeX, 54, 8, 3, 1, SSD1306_WHITE);
    display.drawLine(113, 13, 118, 18, SSD1306_WHITE);
    display.drawLine(118, 13, 113, 18, SSD1306_WHITE);
  }
  if (happyBlend > 0.3f || loveBlend > 0.3f) {
    const int16_t floatY = lroundf(2 * ((1 - loveBlend) * sinf(seconds * 4) +
                                       loveBlend * sinf(seconds * 1.8f)));
    drawHeart(10, 42 + floatY, 3);
    const int16_t sparklePulse = lroundf((1 - loveBlend) * (static_cast<int>(seconds * 4) & 1));
    drawSparkle(118, 27 - floatY, 3 + sparklePulse);
    display.drawLine(16 + petSway, 49 + petLift, 19 + petSway, 47 + petLift, SSD1306_WHITE);
    display.drawLine(109 + petSway, 47 + petLift, 112 + petSway, 49 + petLift, SSD1306_WHITE);
  }
  if (sleepBlend > 0.3f) {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    const int16_t floatY = 15 - static_cast<int16_t>((nowMs % 1800) * 12 / 1800);
    display.setCursor(104, floatY);
    display.print(F("Zzz"));
  }
  ++renderedGeneration;
  ++renderedCount;
}

// Transfer at most 120 changed bytes per loop (~3 ms at standard 400 kHz).
// Full display.display() each frame would alone need >23 ms, below 60 FPS.
// This dirty-page sender preserves bandwidth without overclocking the OLED bus.
void serviceDisplay() {
  if (!displayReady) return;
  if (!framePending) {
    if (queuedGeneration == renderedGeneration) return;
    skippedCount += renderedGeneration - queuedGeneration - 1;
    memcpy(pendingFrame, display.getBuffer(), FRAME_BYTES);
    queuedGeneration = renderedGeneration;
    framePending = true;
  }
  for (uint8_t checked = 0; checked < 8; ++checked) {
    const uint8_t page = (scanPage + checked) % 8;
    const size_t base = page * 128;
    int first = 0, last = 127;
    while (first < 128 && pendingFrame[base + first] == displayedFrame[base + first]) ++first;
    if (first == 128) continue;
    while (last > first && pendingFrame[base + last] == displayedFrame[base + last]) --last;
    last = min(last, first + 119);
    Wire.beginTransmission(OLED_ADDRESS);
    Wire.write(0x00); // command stream, horizontal address mode from begin()
    Wire.write(SSD1306_COLUMNADDR);
    Wire.write(first);
    Wire.write(last);
    Wire.write(SSD1306_PAGEADDR);
    Wire.write(page);
    Wire.write(page);
    uint8_t error = Wire.endTransmission();
    if (!error) {
      Wire.beginTransmission(OLED_ADDRESS);
      Wire.write(0x40); // pixel data
      Wire.write(pendingFrame + base + first, last - first + 1);
      error = Wire.endTransmission();
    }
    if (!error) {
      memcpy(displayedFrame + base + first, pendingFrame + base + first, last - first + 1);
      displayErrors = 0;
    } else if (++displayErrors >= 3) {
      displayReady = false;
      Serial.println(F("OLED I2C failed; distance, touch and servo continue."));
    }
    scanPage = (page + 1) % 8;
    return;
  }
  framePending = false;
  ++presentedCount;
}

void serviceNeck(uint32_t now) {
  if (static_cast<uint32_t>(now - lastServoMs) < 20) return;
  const float dt = min(0.1f, (now - lastServoMs) * 0.001f);
  lastServoMs = now;
  const float phase = (now - moodStartedMs) * 0.001f;
  float target = 90;
  switch (mood) {
    case Mood::Idle: target += sleeping ? 0 : gazeX; break;
    case Mood::Happy: target += 38 * sinf(phase * 5.5f); break;
    case Mood::Angry: target = 180; break;
    case Mood::Love: break; // The affection blend below also eases the release.
  }
  const float petTarget = 98 + 3 * sinf(now * 0.0021f);
  target += (petTarget - target) * loveBlend;
  const float rate = mood == Mood::Love ? 3.5f : mood == Mood::Angry ? 8 : 12;
  neckAngle = approach(neckAngle, target, rate, dt);
  neck.write(constrain(static_cast<int>(lroundf(neckAngle)), 0, 180));
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
  const char *name = mood == Mood::Love ? "LOVE" : mood == Mood::Angry ? "ANGRY" :
      mood == Mood::Happy ? "HAPPY" : sleeping ? "IDLE/SLEEPY" : "IDLE/LOOK";
  char cm[20], message[180];
  if (distanceCm < 0) snprintf(cm, sizeof(cm), "no echo");
  else snprintf(cm, sizeof(cm), "%.1f cm", distanceCm);
  snprintf(message, sizeof(message),
      "Distance=%s | %s | Pet=%s | Servo=%d | FPS draw=%.1f OLED=%.1f | skipped=%lu",
      cm, name, petHeld ? "touched" : "off", static_cast<int>(lroundf(neckAngle)),
      renderFps, presentFps, static_cast<unsigned long>(skippedCount));
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
  pinMode(TRIG_PIN, OUTPUT);
  digitalWrite(TRIG_PIN, LOW);
  pinMode(ECHO_PIN, INPUT);
  pinMode(PET_PIN, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(ECHO_PIN), echoEdge, CHANGE);
  randomSeed(esp_random());

  Wire.begin(SDA_PIN, SCL_PIN, 400000);
  Wire.setTimeOut(10);
  Wire.beginTransmission(OLED_ADDRESS);
  displayReady = Wire.endTransmission() == 0 &&
      display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS, false, false);
  if (displayReady) {
    display.clearDisplay();
    display.display(); // Initial clear only; runtime sends changed regions.
    display.setTextWrap(false);
  } else {
    Serial.println(F("OLED unavailable at 0x3C. Continuing without display."));
  }

  ESP32PWM::allocateTimer(0);
  neck.setPeriodHertz(50);
  neck.attach(SERVO_PIN, 500, 2500);
  neck.write(90);
  if (!neck.attached()) Serial.println(F("ERROR: servo attachment failed."));

  const uint32_t now = millis();
  lastInteractionMs = moodStartedMs = lastServoMs = statsStartedMs = now;
  nextBlinkMs = now + 2400;
  previousFrameUs = micros();
  nextFrameUs = previousFrameUs;
  lastTriggerUs = previousFrameUs; // Let the sensor settle for 80 ms.
  Serial.println(F("Desk Companion Bot: 60 Hz solid eyes, 4 moods, TTP223 touch GPIO4."));
}

void loop() {
  const uint32_t nowUs = micros();
  const uint32_t nowMs = millis();
  if (serviceSonar(nowUs, nowMs)) return;
  servicePet(nowMs);
  updateMood(nowMs);
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
