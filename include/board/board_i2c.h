#pragma once

#include "platform/i2c.h"

#define I2C_TIMING_100KHZ  0xE0941F27U
#define I2C_TIMING_400KHZ  0xE0320309U

static i2c_config_t batt_i2c_config = {
  .i2c = I2C4,
  .timing = I2C_TIMING_100KHZ
};

void board_i2c_init(void);
