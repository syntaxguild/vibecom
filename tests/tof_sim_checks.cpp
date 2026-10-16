// Exercise the actual chip C callbacks through the pinned, unmodified Pololu driver.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <VL53L1X.h>
#include "wokwi-api.h"
#include "behavior.h"
#include "distance_reading.h"

static i2c_config_t device;
static timer_config_t sampleTimer;
static uint32_t distanceCm = 100, target = 1;
static uint64_t nowUs = 0, nextSampleUs = 0;
static uint32_t periodUs = 0;
static bool running = false, powered = true;
TwoWire Wire;

extern "C" {
pin_t pin_init(const char *name, uint32_t) { return !strcmp(name, "VIN") ? 1 : 2; }
uint32_t pin_read(pin_t pin) { return pin == 1 && powered; }
uint32_t attr_init(const char *name, uint32_t) { return !strcmp(name, "distance") ? 1 : 2; }
uint32_t attr_read(uint32_t attr) { return attr == 1 ? distanceCm : target; }
chip_timer_t timer_init(const timer_config_t *config) { sampleTimer = *config; return 1; }
void timer_start(chip_timer_t, uint32_t micros, bool repeat) {
  assert(repeat); periodUs = micros; nextSampleUs = nowUs + micros; running = true;
}
void timer_stop(chip_timer_t) { running = false; }
uint32_t i2c_init(const i2c_config_t *config) { device = *config; return 1; }
}

static void advance(uint64_t micros) {
  const uint64_t end = nowUs + micros;
  while (running && nextSampleUs <= end) {
    nowUs = nextSampleUs; nextSampleUs += periodUs;
    sampleTimer.callback(sampleTimer.user_data);
  }
  nowUs = end;
}
unsigned long millis() { return static_cast<uint32_t>(nowUs / 1000); }
void delay(unsigned long ms) { advance(ms * 1000); }
void delayMicroseconds(unsigned int us) { advance(us); }
void TwoWire::beginTransmission(uint8_t addr) { address = addr; tx.clear(); }
size_t TwoWire::write(uint8_t value) { tx.push_back(value); return 1; }
uint8_t TwoWire::endTransmission() {
  if (address != device.address || !device.connect(device.user_data, address, false)) return 2;
  for (uint8_t byte : tx) if (!device.write(device.user_data, byte)) return 3;
  return 0;
}
uint8_t TwoWire::requestFrom(uint8_t addr, uint8_t count) {
  rx.clear(); cursor = 0;
  if (addr != device.address || !device.connect(device.user_data, addr, true)) return 0;
  for (uint8_t i = 0; i < count; ++i) rx.push_back(device.read(device.user_data));
  return count;
}
int TwoWire::read() { return cursor < rx.size() ? rx[cursor++] : -1; }

int main() {
  chip_init();
  assert(device.address == 0x29);
  VL53L1X sensor;
  sensor.setTimeout(100);
  assert(sensor.init());
  assert(sensor.setDistanceMode(VL53L1X::Long));
  assert(sensor.setMeasurementTimingBudget(50000));
  sensor.startContinuous(80);
  assert(periodUs == 80000 && !sensor.dataReady());
  advance(79000); assert(!sensor.dataReady());
  advance(1000); assert(sensor.dataReady());
  assert(sensor.read(false) == 1000 && sensor.ranging_data.range_status == VL53L1X::RangeValid);
  assert(!sensor.dataReady()); // Driver's interrupt-clear waits for the next sample.
  for (uint32_t cm : {4U, 10U, 30U, 100U, 400U}) {
    distanceCm = cm;
    advance(80000);
    assert(sensor.dataReady());
    assert(sensor.read(false) == cm * 10);
    assert(sensor.ranging_data.range_status == VL53L1X::RangeValid);
    assert(!sensor.timeoutOccurred());
  }
  // Feed this chip + real driver into production filtering, not synthetic cm values.
  Companion bot;
  bot.begin(millis()); distanceCm = 30;
  for (int i = 0; i < 7; ++i) {
    advance(80000);
    const uint16_t mm = sensor.read(false);
    bot.acceptDistance(tofDistanceCm(mm, sensor.ranging_data.range_status, !sensor.last_status), millis());
    bot.update(false, millis());
  }
  assert(bot.mood == Mood::Happy);
  target = 0;
  for (int i = 0; i < 8; ++i) {
    advance(80000); const uint16_t mm = sensor.read(false);
    assert(sensor.ranging_data.range_status == VL53L1X::SignalFail);
    bot.acceptDistance(tofDistanceCm(mm, sensor.ranging_data.range_status, !sensor.last_status), millis());
    bot.update(false, millis());
  }
  assert(bot.distanceCm < 0 && bot.mood == Mood::Idle);
  target = 1; distanceCm = 10;
  for (int i = 0; i < 7; ++i) {
    advance(80000); const uint16_t mm = sensor.read(false);
    bot.acceptDistance(tofDistanceCm(mm, sensor.ranging_data.range_status, !sensor.last_status), millis());
    bot.update(false, millis());
  }
  assert(bot.mood == Mood::Angry);
  sensor.stopContinuous(); advance(100000); assert(!sensor.dataReady());
  powered = false;
  VL53L1X unplugged; assert(!unplugged.init());
  puts("PASS: actual VL53L1X chip callbacks + Pololu init/timing/I2C, slider units, missing target, Happy/Idle/Angry, unpowered NACK");
}
