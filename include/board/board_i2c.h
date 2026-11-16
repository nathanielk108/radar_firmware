#pragma once

#include "platform/i2c.h"

extern const i2c_config_t batt_i2c_config;

void board_i2c_batt_init(void);
