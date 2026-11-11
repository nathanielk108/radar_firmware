#pragma once

#include "platform/i2c.h"

#define I2C_TIMING_100KHZ  0xE0941F27U
#define I2C_TIMING_400KHZ  0xE0320309U

extern const i2c_config_t batt_i2c_config;

void board_i2c_init(void);

