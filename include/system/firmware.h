#pragma once

#include <stdbool.h>
#include <stdint.h>

#define FIRMWARE_GAIN_MIN_MV 200U
#define FIRMWARE_GAIN_MAX_MV 3100U

typedef struct {
  bool usb_ready;
  bool vbus_present;
  bool charge_inhibited;
  bool efuse_power_good;
  bool radar_ready;
} firmware_status_t;

bool firmware_init(void);
void firmware_poll(void);
firmware_status_t firmware_status(void);

/* Foreground only. RF power and gain changes require idle acquisition. */
bool firmware_rf_enable(bool enable);
bool firmware_gain_set(uint16_t millivolts);
bool firmware_gain_get(uint16_t *millivolts);
