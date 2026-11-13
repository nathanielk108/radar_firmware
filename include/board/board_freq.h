#pragma once

#include "drivers/adf4158.h"

extern const adf4158_ramp_config_t board_adf4158_ramp;
extern const adf4158_fixed_config_t board_adf4158_fixed;

void board_freq_init(void);
