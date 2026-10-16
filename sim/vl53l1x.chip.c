// Limited I2C simulation for the pinned Pololu VL53L1X 1.3.1 driver.
// Models init, continuous data-ready, distance and missing targets; not optics,
// crosstalk, real calibration, power consumption or every sensor register.
#include "wokwi-api.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  uint8_t registers[0x120];
  uint16_t pointer;
  uint8_t address_bytes, stream;
  uint32_t distance_attr, target_attr;
  pin_t vin, gnd;
  timer_t timer;
} chip_state_t;

static void reset_sensor(chip_state_t *chip) {
  memset(chip->registers, 0, sizeof(chip->registers));
  chip->registers[0x010f] = 0xea; // model ID/module type, checked by init()
  chip->registers[0x0110] = 0xcc;
  chip->registers[0x00e5] = 1;    // firmware boot complete
  chip->registers[0x0006] = 0x0a; // nonzero oscillator values for timing math
  chip->registers[0x00df] = 1;
  chip->registers[0x0031] = 1;    // active-low data-ready: not ready
  chip->stream = 0;
  timer_stop(chip->timer);
}

static void sample_ready(void *user_data) {
  chip_state_t *chip = user_data;
  uint32_t cm = attr_read(chip->distance_attr);
  if (cm > 400) cm = 400;
  // Invert the Pololu driver's 2011/2048 range gain so slider cm == reported cm.
  const uint16_t raw = (cm * 10 * 2048 + 1005) / 2011;
  if (++chip->stream == 0) ++chip->stream;
  chip->registers[0x0089] = attr_read(chip->target_attr) ? 9 : 4;
  chip->registers[0x008b] = chip->stream;
  chip->registers[0x008c] = 1; // effective SPAD count = 256
  chip->registers[0x0096] = raw >> 8;
  chip->registers[0x0097] = raw;
  chip->registers[0x0098] = 4; // nonzero signal rate for driver DSS calculation
  chip->registers[0x0031] = 0;
}

static bool on_connect(void *user_data, uint32_t address, bool read) {
  chip_state_t *chip = user_data;
  (void)address;
  if (!read) chip->address_bytes = 0;
  return pin_read(chip->vin) && !pin_read(chip->gnd);
}

static uint8_t on_read(void *user_data) {
  chip_state_t *chip = user_data;
  const uint16_t reg = chip->pointer++;
  return reg < sizeof(chip->registers) ? chip->registers[reg] : 0;
}

static bool on_write(void *user_data, uint8_t value) {
  chip_state_t *chip = user_data;
  if (chip->address_bytes < 2) {
    chip->pointer = chip->address_bytes++ == 0 ? (uint16_t)value << 8 : chip->pointer | value;
    return true;
  }
  const uint16_t reg = chip->pointer++;
  if (reg >= sizeof(chip->registers)) return true;
  chip->registers[reg] = value;
  if (reg == 0 && value == 0) reset_sensor(chip);
  if (reg == 0x0086 && (value & 1)) chip->registers[0x0031] = 1;
  if (reg == 0x0087) {
    chip->registers[0x0031] = 1;
    if (value == 0x40) {
      uint32_t period = 0;
      for (uint16_t i = 0x006c; i <= 0x006f; ++i) period = (period << 8) | chip->registers[i];
      if (period < 20) period = 20;
      if (period > 1000) period = 1000;
      timer_start(chip->timer, period * 1000, true);
    } else timer_stop(chip->timer);
  }
  return true;
}

void chip_init(void) {
  chip_state_t *chip = calloc(1, sizeof(chip_state_t));
  chip->vin = pin_init("VIN", INPUT);
  chip->gnd = pin_init("GND", INPUT);
  chip->distance_attr = attr_init("distance", 100);
  chip->target_attr = attr_init("target", 1);
  const timer_config_t timer_config = {.callback = sample_ready, .user_data = chip};
  chip->timer = timer_init(&timer_config);
  reset_sensor(chip);
  const i2c_config_t config = {
    .address = 0x29,
    .scl = pin_init("SCL", INPUT_PULLUP),
    .sda = pin_init("SDA", INPUT_PULLUP),
    .connect = on_connect,
    .read = on_read,
    .write = on_write,
    .user_data = chip,
  };
  i2c_init(&config);
}
