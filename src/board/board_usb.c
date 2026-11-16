#include "board/board_usb.h"
#include "board/board_gpio.h"
#include "board/board_pins.h"
#include "board/board_power.h"

void board_usb_init(void) {
  board_gpio_pin(USB_DP_PORT, USB_DP_PIN, LL_GPIO_MODE_ALTERNATE,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_HIGH, USB_DATA_AF, false);
  board_gpio_pin(USB_DM_PORT, USB_DM_PIN, LL_GPIO_MODE_ALTERNATE,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_HIGH, USB_DATA_AF, false);
}

/* R32/R33 divide 5 V VBUS to 1.6 V at PA9. Use the charger's qualified VBUS. */
bool board_usb_vbus_present(void) { return board_battery_power_good(); }
