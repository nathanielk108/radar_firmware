#include "board/board_power.h"
#include "board/board_gpio.h"
#include "board/board_pins.h"

board_power_state_t board_power_init(void) {
  /* Active-low charger enable: keep charging inhibited at boot. */
  board_gpio_pin(BATT_CE_PORT, BATT_CE_PIN, LL_GPIO_MODE_OUTPUT,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_LOW, 0U, true);

  /* Both RF rails must remain disabled until explicitly enabled. */
  board_gpio_pin(RF_5V_EN_PORT, RF_5V_EN_PIN, LL_GPIO_MODE_OUTPUT,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_LOW, 0U, false);
  board_gpio_pin(RF_15V_EN_PORT, RF_15V_EN_PIN, LL_GPIO_MODE_OUTPUT,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_LOW, 0U, false);

  board_gpio_pin(EFUSE_PG_PORT, EFUSE_PG_PIN, LL_GPIO_MODE_INPUT,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_LOW, 0U, false);
  board_gpio_pin(BATT_PG_PORT, BATT_PG_PIN, LL_GPIO_MODE_INPUT,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_LOW, 0U, false);
  board_gpio_pin(BATT_INT_PORT, BATT_INT_PIN, LL_GPIO_MODE_INPUT,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_LOW, 0U, false);
  return BOARD_POWER_RF_OFF;
}

void board_rf_5v_enable(bool enable) {
  if (enable)
    LL_GPIO_SetOutputPin(RF_5V_EN_PORT, 1UL << RF_5V_EN_PIN);
  else
    LL_GPIO_ResetOutputPin(RF_5V_EN_PORT, 1UL << RF_5V_EN_PIN);
}

void board_rf_15v_enable(bool enable) {
  if (enable)
    LL_GPIO_SetOutputPin(RF_15V_EN_PORT, 1UL << RF_15V_EN_PIN);
  else
    LL_GPIO_ResetOutputPin(RF_15V_EN_PORT, 1UL << RF_15V_EN_PIN);
}

bool board_efuse_power_good(void) {
  return LL_GPIO_IsInputPinSet(EFUSE_PG_PORT, 1UL << EFUSE_PG_PIN);
}

void board_battery_charge_inhibit(bool inhibit) {
  if (inhibit)
    LL_GPIO_SetOutputPin(BATT_CE_PORT, 1UL << BATT_CE_PIN);
  else
    LL_GPIO_ResetOutputPin(BATT_CE_PORT, 1UL << BATT_CE_PIN);
}

bool board_battery_power_good(void) {
  return !LL_GPIO_IsInputPinSet(BATT_PG_PORT, 1UL << BATT_PG_PIN);
}

bool board_battery_interrupt_active(void) {
  return !LL_GPIO_IsInputPinSet(BATT_INT_PORT, 1UL << BATT_INT_PIN);
}

bool board_battery_charge_inhibited(void) {
  return LL_GPIO_IsOutputPinSet(BATT_CE_PORT, 1UL << BATT_CE_PIN);
}
