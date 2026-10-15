#include <Arduino.h>
#include <SPI.h>
#include "face.h"
#ifdef WOKWI_ILI9341
#include <Adafruit_ILI9341.h>
#else
#include <Adafruit_ST7789.h>
#endif
#include <ESP32Servo.h>
#include <driver/gpio.h>
#include <math.h>
#include <string.h>

namespace {
// GPIO18 and GPIO19 remain dedicated to the servo and sonar, not default VSPI.
constexpr uint8_t TFT_SCLK = 14, TFT_MOSI = 23, TFT_DC = 26, TFT_RST = 33;
constexpr uint32_t TFT_SPI_HZ = 40000000; // Reduce to 20000000 for long/noisy wires.
#ifdef WOKWI_ILI9341
constexpr int8_t TFT_CS = 27;
constexpr int16_t FACE_TOP = 80; // Center the same 240x160 face on the simulator's 240x320 LCD.
#else
constexpr int8_t TFT_CS = -1; // The 7-pin ST7789 module has no exposed CS.
constexpr int16_t FACE_TOP = 40;
constexpr uint8_t TFT_SPI_MODE = SPI_MODE0;
constexpr bool TFT_INVERT = true;
#endif
constexpr uint8_t SERVO_PIN = 18, TRIG_PIN = 5, ECHO_PIN = 19;
constexpr uint8_t PET_PIN = 4; // TTP223 OUT: HIGH while touched, LOW when released.
constexpr uint32_t FRAME_US = 16667, SENSOR_PERIOD_US = 80000;
constexpr uint32_t ECHO_TIMEOUT_US = 30000, SLEEP_AFTER_MS = 10000;
constexpr float PI_F = 3.14159265f;

enum class SonarPhase : uint8_t { Waiting, TriggerHigh, Listening };

SPIClass displaySPI(VSPI);
#ifdef WOKWI_ILI9341
Adafruit_ILI9341 display(&displaySPI, TFT_DC, TFT_CS, TFT_RST);
#else
Adafruit_ST7789 display(&displaySPI, TFT_CS, TFT_DC, TFT_RST);
#endif
GFXcanvas16 frameA(FACE_WIDTH, FACE_HEIGHT), frameB(FACE_WIDTH, FACE_HEIGHT);
GFXcanvas16 *drawingFrame = &frameA, *sendingFrame = &frameB;
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

// Render into one RGB565 canvas while the other is sent unchanged in short bands.
// No clear-and-redraw on the physical panel, and no PSRAM required.
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
  if (!displayReady) return;
  const FaceState face = {mood, happyBlend, angryBlend, loveBlend, sleepBlend,
                          gazeX, gazeY, openness};
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
    GFXcanvas16 *available = sendingFrame;
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
    display.startWrite();
    display.setAddrWindow(0, FACE_TOP + sendRow, FACE_WIDTH, FACE_BAND_ROWS);
    display.writePixels(sendingFrame->getBuffer() + sendRow * FACE_WIDTH,
                        FACE_BAND_ROWS * FACE_WIDTH);
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
  char cm[20], message[240];
  if (distanceCm < 0) snprintf(cm, sizeof(cm), "no echo");
  else snprintf(cm, sizeof(cm), "%.1f cm", distanceCm);
  snprintf(message, sizeof(message),
      "Distance=%s | %s | Pet=%s | Servo=%d | FPS draw=%.1f TFT=%.1f | skipped=%lu | SPI max=%.2f ms",
      cm, name, petHeld ? "touched" : "off", static_cast<int>(lroundf(neckAngle)),
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
  pinMode(TRIG_PIN, OUTPUT);
  digitalWrite(TRIG_PIN, LOW);
  pinMode(ECHO_PIN, INPUT);
  pinMode(PET_PIN, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(ECHO_PIN), echoEdge, CHANGE);
  randomSeed(esp_random());

  displayReady = frameA.getBuffer() && frameB.getBuffer();
  if (displayReady) {
    displaySPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
#ifdef WOKWI_ILI9341
    display.begin(TFT_SPI_HZ);
    display.invertDisplay(false);
#else
    display.init(240, 240, TFT_SPI_MODE);
    display.setSPISpeed(TFT_SPI_HZ);
    display.invertDisplay(TFT_INVERT);
#endif
    display.setRotation(0);
    display.fillScreen(0); // Clear once, before sonar is armed.
  } else {
    Serial.println(F("ERROR: RGB565 canvas allocation failed; distance, touch and servo continue."));
  }
  Serial.printf("Display buffers: 153600 bytes | free heap=%u\n", ESP.getFreeHeap());

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
  Serial.println(F("Desk Companion Bot: 60 Hz color face, SPI TFT, 4 moods, TTP223 GPIO4."));
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
