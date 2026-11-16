#include "board/board_i2c.h"
#include "board/board_gpio.h"
#include "board/board_pins.h"

/* PCLK4 at 120 MHz. The prescaled high and low phases are 5 us each. */
const i2c_config_t batt_i2c_config = {.i2c = BATT_I2C, .timing = 0xE0942727U};

void board_i2c_batt_init(void) {
  board_gpio_pin(BATT_SCL_PORT, BATT_SCL_PIN, LL_GPIO_MODE_ALTERNATE,
                 LL_GPIO_OUTPUT_OPENDRAIN, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_HIGH, BATT_SCL_AF, false);

  board_gpio_pin(BATT_SDA_PORT, BATT_SDA_PIN, LL_GPIO_MODE_ALTERNATE,
                 LL_GPIO_OUTPUT_OPENDRAIN, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_HIGH, BATT_SDA_AF, false);
}
