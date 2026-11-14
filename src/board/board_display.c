#include "board/board_display.h"
#include "board/board_gpio.h"
#include "board/board_pins.h"

void board_display_init(void) {
  board_gpio_pin(DISPLAY_BL_PORT, DISPLAY_BL_PIN, LL_GPIO_MODE_OUTPUT,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_LOW, 0U, false);
}
