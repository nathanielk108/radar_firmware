#include "services/display.h"

#include "board/board_display.h"
#include "drivers/st7789.h"

_Static_assert(DISPLAY_WIDTH == ST7789_WIDTH && DISPLAY_HEIGHT == ST7789_HEIGHT,
               "Display geometry must match the installed panel");

static bool active;

display_status_t display_enable(bool enabled) {
  if (enabled == active)
    return DISPLAY_OK;
  if (enabled) {
    if (!board_display_bus_start())
      return DISPLAY_UNAVAILABLE;
    st7789_init();
    st7789_fill(0U, 0U, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0U);
    board_display_backlight(true);
  } else {
    board_display_backlight(false);
    board_display_reset(true);
    board_display_bus_stop();
  }
  active = enabled;
  return DISPLAY_OK;
}

bool display_enabled(void) { return active; }

const char *display_status_name(display_status_t status) {
  switch (status) {
  case DISPLAY_OK:
    return "OK";
  case DISPLAY_DISABLED:
    return "disabled";
  case DISPLAY_UNAVAILABLE:
    return "FMC clock unavailable";
  case DISPLAY_INVALID_ARGUMENT:
    return "invalid argument";
  default:
    return "unknown";
  }
}

static bool valid_rect(uint16_t x, uint16_t y, uint16_t width,
                       uint16_t height) {
  return width && height && (uint32_t)x + width <= DISPLAY_WIDTH &&
         (uint32_t)y + height <= DISPLAY_HEIGHT;
}

display_status_t display_fill(uint16_t x, uint16_t y, uint16_t width,
                              uint16_t height, uint16_t rgb565) {
  if (!valid_rect(x, y, width, height))
    return DISPLAY_INVALID_ARGUMENT;
  if (!active)
    return DISPLAY_DISABLED;
  st7789_fill(x, y, width, height, rgb565);
  return DISPLAY_OK;
}

display_status_t display_blit(uint16_t x, uint16_t y, uint16_t width,
                              uint16_t height, const uint16_t *pixels) {
  if (!pixels || !valid_rect(x, y, width, height))
    return DISPLAY_INVALID_ARGUMENT;
  if (!active)
    return DISPLAY_DISABLED;
  st7789_blit(x, y, width, height, pixels);
  return DISPLAY_OK;
}

display_status_t display_test_pattern(void) {
  if (!active)
    return DISPLAY_DISABLED;
  static const uint16_t bars[] = {0xF800U, 0x07E0U, 0x001FU, 0xFFFFU, 0U};
  uint16_t width = DISPLAY_WIDTH / (sizeof(bars) / sizeof(bars[0]));
  for (unsigned i = 0; i < sizeof(bars) / sizeof(bars[0]); ++i)
    st7789_fill((uint16_t)(i * width), 0U, width, DISPLAY_HEIGHT, bars[i]);
  st7789_fill(0U, 0U, 16U, 32U, 0xFFFFU);
  return DISPLAY_OK;
}
