#pragma once

#include <stdbool.h>
#include <stdint.h>

void board_display_init(void);
bool board_display_bus_start(void);
void board_display_bus_stop(void);
void board_display_reset(bool asserted);
/* Active-high PWM duty cycle, from 0 (off) to 100 (fully on). */
void board_display_backlight(uint8_t percent);
void board_display_command(uint8_t command);
void board_display_data(uint16_t data);
