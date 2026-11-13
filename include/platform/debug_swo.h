#pragma once

#include <stdint.h>
#include <stdbool.h>

void debug_swo_init(void);
bool debug_swo_putc(char c);
void debug_swo_write(const char *str);
void debug_swo_hex32(uint32_t value);
