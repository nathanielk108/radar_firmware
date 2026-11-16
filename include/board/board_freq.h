#pragma once

#include "drivers/adf4158.h"

extern const adf4158_ramp_config_t board_adf4158_ramp;
extern const adf4158_fixed_config_t board_adf4158_fixed;

void board_freq_init(void);
void board_freq_hold_txdata_low(void);
/* TIM2_CH1 must be driving the inactive level before selecting its AF. */
void board_freq_use_txdata_timer(void);
