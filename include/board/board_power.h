#pragma once

#include <stdbool.h>

void board_power_init(void);
void board_rf_power_on(void);
void board_rf_power_off(void);
void board_lcd_power_enable(bool enable);
void board_aux_power_enable(bool enable_5v, bool enable_15v);
bool board_power_5v_enabled(void);
bool board_power_15v_enabled(void);

bool board_efuse_power_good(void);

/* BQ25883 external status and active-low charge inhibit. */
void board_battery_charge_inhibit(bool inhibit);
bool board_battery_power_good(void);
bool board_battery_interrupt_active(void);

/* True when PF13 is high (charging inhibited). */
bool board_battery_charge_inhibited(void);
