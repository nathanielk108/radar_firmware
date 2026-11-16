#pragma once

#include <stdbool.h>
#include <stdint.h>

#define DISPLAY_WIDTH 240U
#define DISPLAY_HEIGHT 320U

typedef enum {
  DISPLAY_OK,
  DISPLAY_DISABLED,
  DISPLAY_UNAVAILABLE,
  DISPLAY_INVALID_ARGUMENT
} display_status_t;

/* Disabled at boot. Enable only when a panel is fitted; enabled is a software
 * state. Calls are synchronous and intended for foreground use while idle.
 */
display_status_t display_enable(bool enabled);
bool display_enabled(void);
const char *display_status_name(display_status_t status);

display_status_t display_fill(uint16_t x, uint16_t y, uint16_t width,
                              uint16_t height, uint16_t rgb565);
/* pixels contains width * height RGB565 values in row-major order. */
display_status_t display_blit(uint16_t x, uint16_t y, uint16_t width,
                              uint16_t height, const uint16_t *pixels);
display_status_t display_test_pattern(void);
