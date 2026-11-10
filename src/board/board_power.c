#include "board/board_power.h"
#include "board/board_pins.h"
#include "platform/gpio.h"

board_power_state_t board_power_init(void) {
  gpio_config_t efuse_pg_conf = {
    .port = EFUSE_PG_PORT,
    .pin = EFUSE_PG_PIN,
    .mode = GPIO_MODE_INPUT,
    .output_type = GPIO_PUSH_PULL,
    .pull = GPIO_PULL_NONE,
    .speed = GPIO_SPEED_LOW
  };

  gpio_config_t batt_pg_conf = {
    .port = BATT_PG_PORT,
    .pin = BATT_PG_PIN,
    .mode = GPIO_MODE_INPUT,
    .output_type = GPIO_PUSH_PULL,
    .pull = GPIO_PULL_NONE,
    .speed = GPIO_SPEED_LOW
  };

  gpio_config_t batt_ce_conf = {
    .port = BATT_CE_PORT,
    .pin = BATT_CE_PIN,
    .mode = GPIO_MODE_OUTPUT,
    .output_type = GPIO_PUSH_PULL,
    .pull = GPIO_PULL_NONE,
    .speed = GPIO_SPEED_LOW,
    .init_state = true
  };

  gpio_config_t rf_5v_enable_conf = {
    .port = RF_5V_EN_PORT,
    .pin = RF_5V_EN_PIN,
    .mode = GPIO_MODE_OUTPUT,
    .output_type = GPIO_PUSH_PULL,
    .pull = GPIO_PULL_NONE,
    .speed = GPIO_SPEED_LOW,
    .init_state = false
  };

  gpio_config_t rf_15v_enable_conf = {
    .port = RF_15V_EN_PORT,
    .pin = RF_15V_EN_PIN,
    .mode = GPIO_MODE_OUTPUT,
    .output_type = GPIO_PUSH_PULL,
    .pull = GPIO_PULL_NONE,
    .speed = GPIO_SPEED_LOW,
    .init_state = false
  };

  gpio_init(&efuse_pg_conf);
  gpio_init(&batt_pg_conf);
  gpio_init(&batt_ce_conf);
  gpio_init(&rf_5v_enable_conf);
  gpio_init(&rf_15v_enable_conf);

  gpio_write(RF_5V_EN_PORT, RF_5V_EN_PIN, false);
  gpio_write(RF_15V_EN_PORT, RF_15V_EN_PIN, false);

  return BOARD_POWER_RF_OFF;
}

void board_rf_5v_enable(bool enable) {
  gpio_write(RF_5V_EN_PORT, RF_5V_EN_PIN, enable);
}

void board_rf_15v_enable(bool enable) {
  gpio_write(RF_15V_EN_PORT, RF_15V_EN_PIN, enable);
}

bool board_efuse_power_good(void) {
  return gpio_read(EFUSE_PG_PORT, EFUSE_PG_PIN);
}

bool board_battery_power_good(void) {
  // BQ25883 has active low power good output
  return !gpio_read(BATT_PG_PORT, BATT_PG_PIN);
}
