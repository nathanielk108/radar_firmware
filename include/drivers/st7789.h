#pragma once

#include <stdint.h>

#define ST7789_WIDTH 240U
#define ST7789_HEIGHT 320U

/* FMC and the display reset GPIO must be initialized by the board layer. */
void st7789_init(void);
void st7789_fill(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                 uint16_t color);
void st7789_blit(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                 const uint16_t *pixels);
