#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BOARD_GAIN_DEFAULT_MV 2500U
#define BOARD_GAIN_MIN_MV 200U
#define BOARD_GAIN_MAX_MV 3100U

/* Initialize the common VGA gain output after the RF rails are up. */
void board_gain_init(void);

/* Set nominal output voltage between captures. */
bool board_gain_set_mv(uint16_t millivolts);
uint16_t board_gain_get_mv(void);
