#include "board/board_i2c.h"
#include "board/board_pins.h"
#include "platform/gpio.h"

const i2c_config_t batt_i2c_config = {
  .i2c = I2C4,
  .timing = I2C_TIMING_100KHZ
};

void board_i2c_init(void) {

  gpio_config_t batt_scl = {
    .port        = BATT_SCL_PORT,
    .pin         = BATT_SCL_PIN,
    .mode        = GPIO_MODE_ALTERNATE,
    .output_type = GPIO_OPEN_DRAIN,
    .pull        = GPIO_PULL_NONE,
    .speed       = GPIO_SPEED_HIGH,
    .alternate   = BATT_SCL_AF,
  };

  gpio_config_t batt_sda = {
    .port        = BATT_SDA_PORT,
    .pin         = BATT_SDA_PIN,
    .mode        = GPIO_MODE_ALTERNATE,
    .output_type = GPIO_OPEN_DRAIN,
    .pull        = GPIO_PULL_NONE,
    .speed       = GPIO_SPEED_HIGH,
    .alternate   = BATT_SDA_AF,
  };

  gpio_init(&batt_scl);
  gpio_init(&batt_sda);
}

