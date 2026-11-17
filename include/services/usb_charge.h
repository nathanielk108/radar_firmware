#pragma once

#include "drivers/bq25883.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  USB_BATTERY_UNKNOWN,
  USB_BATTERY_ABSENT,
  USB_BATTERY_PRESENT
} usb_battery_presence_t;

typedef struct {
  bool ready;
  bool update_failed;
  uint16_t cc1_mv;
  uint16_t cc2_mv;
  uint16_t advertised_ma;
  uint16_t input_limit_ma;
  usb_battery_presence_t battery_presence;
} usb_charge_status_t;

void usb_charge_init(void);
void usb_charge_task(void);
usb_charge_status_t usb_charge_status(void);
/* Returns the last completed charger ADC conversion while VBUS is present. */
bool usb_charge_get_snapshot(bq25883_snapshot_t *snapshot, uint32_t *age_ms);
