#pragma once
// Host shim for the API calls used by our chip. The real WASM build uses Wokwi's SDK.
#include <stdbool.h>
#include <stdint.h>
typedef int32_t pin_t;
typedef uint32_t chip_timer_t;
#ifndef __cplusplus
#define timer_t chip_timer_t
#endif
enum { INPUT = 0, INPUT_PULLUP = 2 };
typedef struct {
  void *user_data;
  void (*callback)(void *);
  uint32_t reserved[8];
} timer_config_t;
typedef struct {
  void *user_data;
  uint32_t address;
  pin_t scl, sda;
  bool (*connect)(void *, uint32_t, bool);
  uint8_t (*read)(void *);
  bool (*write)(void *, uint8_t);
  void (*disconnect)(void *);
  uint32_t reserved[8];
} i2c_config_t;
#ifdef __cplusplus
extern "C" {
#endif
pin_t pin_init(const char *, uint32_t);
uint32_t pin_read(pin_t);
uint32_t attr_init(const char *, uint32_t);
uint32_t attr_read(uint32_t);
chip_timer_t timer_init(const timer_config_t *);
void timer_start(chip_timer_t, uint32_t, bool);
void timer_stop(chip_timer_t);
uint32_t i2c_init(const i2c_config_t *);
void chip_init(void);
#ifdef __cplusplus
}
#endif
