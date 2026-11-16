#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Initialize ADC1 after the MCU clock and timebase are ready. */
bool board_usb_cc_init(void);

/* Read both CC pins against the board's nominal 3.3 V ADC reference. */
bool board_usb_cc_read_mv(uint16_t *cc1_mv, uint16_t *cc2_mv);
