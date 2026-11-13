#pragma once

#include <stdint.h>
#include <stdbool.h>

bool time_init(void);

uint32_t time_ms(void);
uint32_t time_elapsed_ms(uint32_t start);
bool time_expired(uint32_t start, uint32_t duration);

/* Busy-wait for coarse board power-up, not for precise sampling. */
void time_delay_ms(uint32_t duration);
