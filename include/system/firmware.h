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
  bool rail_5v_enabled;
  bool rail_15v_enabled;
} firmware_status_t;

typedef enum {
  FIRMWARE_RAILS_OFF,
  FIRMWARE_RAILS_5V,
  FIRMWARE_RAILS_ALL
} firmware_rail_mode_t;

bool firmware_init(void);
/* Foreground motion and housekeeping. Charger I/O can wait for its bus,
 * so service deadline-sensitive ADC blocks before calling this function. */
void firmware_poll(void);
firmware_status_t firmware_status(void);

/* Foreground only. RF power and gain changes require idle acquisition. */
bool firmware_rf_enable(bool enable);
/* Manually sequence regulator enables without probing the RF board. */
bool firmware_rails_set(firmware_rail_mode_t mode);
bool firmware_gain_set(uint16_t millivolts);
bool firmware_gain_get(uint16_t *millivolts);
