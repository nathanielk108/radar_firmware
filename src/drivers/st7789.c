#include "drivers/st7789.h"

#include "board/board_display.h"
#include "platform/time.h"

#define ST7789_SLEEP_OUT 0x11U
#define ST7789_DISPLAY_ON 0x29U
#define ST7789_COLUMN_ADDR 0x2AU
#define ST7789_ROW_ADDR 0x2BU
#define ST7789_MEMORY_WRITE 0x2CU

/* Panel power and gamma parameters from the NHD-2.4-240320CF-CSXN-F data
 * sheet. Each entry is command, parameter count, then parameter bytes.
 */
static const uint8_t panel_config[][16] = {
    {0x36U, 1U, 0x80U}, /* Portrait memory scan. */
    {0x3AU, 1U, 0x55U}, /* 16-bit RGB565 pixel format. */
    {0xB2U, 5U, 0x0CU, 0x0CU, 0x00U, 0x33U, 0x33U},
    {0xB7U, 1U, 0x35U},
    {0xBBU, 1U, 0x2BU},
    {0xC0U, 1U, 0x2CU},
    {0xC2U, 2U, 0x01U, 0xFFU},
    {0xC3U, 1U, 0x11U},
    {0xC4U, 1U, 0x20U},
    {0xC6U, 1U, 0x0FU},
    {0xD0U, 2U, 0xA4U, 0xA1U},
    {0xE0U, 14U, 0xD0U, 0x00U, 0x05U, 0x0EU, 0x15U, 0x0DU, 0x37U, 0x43U,
     0x47U, 0x09U, 0x15U, 0x12U, 0x16U, 0x19U},
    {0xE1U, 14U, 0xD0U, 0x00U, 0x05U, 0x0DU, 0x0CU, 0x06U, 0x2DU, 0x44U,
     0x40U, 0x0EU, 0x1CU, 0x18U, 0x16U, 0x19U},
};

void st7789_init(void) {
  board_display_reset(true);
  time_delay_ms(10U);
  board_display_reset(false);
  time_delay_ms(120U);
  board_display_command(ST7789_SLEEP_OUT);
  time_delay_ms(120U);

  for (unsigned i = 0; i < sizeof(panel_config) / sizeof(panel_config[0]); ++i) {
    const uint8_t *entry = panel_config[i];
    board_display_command(entry[0]);
    for (unsigned j = 0; j < entry[1]; ++j)
      board_display_data(entry[2U + j]);
  }
  board_display_command(ST7789_DISPLAY_ON);
  time_delay_ms(10U);
}

static void write_coordinate(uint16_t coordinate) {
  board_display_data(coordinate >> 8);
  board_display_data(coordinate & 0xFFU);
}

static void set_window(uint16_t x, uint16_t y, uint16_t width,
                       uint16_t height) {
  board_display_command(ST7789_COLUMN_ADDR);
  write_coordinate(x);
  write_coordinate((uint16_t)(x + width - 1U));
  board_display_command(ST7789_ROW_ADDR);
  write_coordinate(y);
  write_coordinate((uint16_t)(y + height - 1U));
  board_display_command(ST7789_MEMORY_WRITE);
}

void st7789_fill(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                 uint16_t color) {
  set_window(x, y, width, height);
  uint32_t count = (uint32_t)width * height;
  for (uint32_t i = 0; i < count; ++i)
    board_display_data(color);
}

void st7789_blit(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                 const uint16_t *pixels) {
  set_window(x, y, width, height);
  uint32_t count = (uint32_t)width * height;
  for (uint32_t i = 0; i < count; ++i)
    board_display_data(pixels[i]);
}
