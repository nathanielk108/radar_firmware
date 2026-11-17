#pragma once

#include "platform/i2c.h"

extern const i2c_config_t batt_i2c_config;

void board_i2c_batt_init(void);
/* Reset the dedicated STM32 controller while the charger is inhibited. */
void board_i2c_batt_reset(void);
