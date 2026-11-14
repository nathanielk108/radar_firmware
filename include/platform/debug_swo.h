#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void debug_swo_init(void);
bool debug_swo_putc(char c);
bool debug_swo_write_bytes(const char *data, size_t length);
void debug_swo_write(const char *str);
void debug_swo_hex32(uint32_t value);
void debug_swo_poll(void); /* Call frequently in main loop. */
void debug_swo_flush_bounded(uint32_t iterations);
uint32_t debug_swo_dropped(void);
