#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  bool ready;
  bool update_failed;
  uint16_t cc1_mv;
  uint16_t cc2_mv;
  uint16_t advertised_ma;
  uint16_t input_limit_ma;
} usb_charge_status_t;

void usb_charge_init(void);
void usb_charge_task(void);
usb_charge_status_t usb_charge_status(void);
