#include "board/board_usb.h"
#include "board/board_gpio.h"
#include "board/board_pins.h"

void board_usb_init(void) {
  board_gpio_pin(USB_DP_PORT, USB_DP_PIN, LL_GPIO_MODE_ALTERNATE,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_HIGH, USB_DATA_AF, false);
  board_gpio_pin(USB_DM_PORT, USB_DM_PIN, LL_GPIO_MODE_ALTERNATE,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_HIGH, USB_DATA_AF, false);
  board_gpio_pin(USB_VBUS_PORT, USB_VBUS_PIN, LL_GPIO_MODE_INPUT,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_LOW, 0U, false);
}
