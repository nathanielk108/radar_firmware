#pragma once

#include <stdbool.h>
#include <stdint.h>

bool time_init(void);

uint32_t time_ms(void);
/* Shared ADC and IMU clock. Subtract as unsigned values across the 2^32 us
 * wrap to measure intervals shorter than one wrap. */
uint32_t time_us(void);
uint32_t time_elapsed_ms(uint32_t start);
bool time_expired(uint32_t start, uint32_t duration);

/* Busy-wait for board power-up or device protocol timing. */
void time_delay_ms(uint32_t duration);
/* Requires the DWT cycle counter. Intended for short delays. */
void time_delay_us(uint32_t duration);
