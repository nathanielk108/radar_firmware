#pragma once

#include "stm32h7xx.h"
#include <stdint.h>
#include <stdbool.h>

typedef enum {
  BOARD_POWER_RF_OFF,
  BOARD_POWER_RF_STARTING,
  BOARD_POWER_RF_ON,
  BOARD_POWER_RF_FAULT,
} board_power_state_t;

board_power_state_t board_power_init(void);

void board_rf_5v_enable(bool enable);
void board_rf_15v_enable(bool enable);

bool board_efuse_power_good(void);
bool board_battery_power_good(void);
