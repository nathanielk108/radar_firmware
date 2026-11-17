#include "board/board_power.h"
#include "board/board_gpio.h"
#include "board/board_pins.h"
#include "platform/time.h"

#define RF_RAIL_SETTLE_MS 200U

static bool rf_power;
static bool lcd_power;
static bool manual_5v;
static bool manual_15v;
static uint32_t five_volt_started_ms;

void board_power_init(void) {
  /* Active-low charger enable: keep charging inhibited at boot. */
  board_gpio_pin(BATT_CE_PORT, BATT_CE_PIN, LL_GPIO_MODE_OUTPUT,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_LOW, 0U, true);

  /* Both auxiliary regulators start disabled. */
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
}

bool board_power_5v_enabled(void) {
  return LL_GPIO_IsOutputPinSet(RF_5V_EN_PORT, 1UL << RF_5V_EN_PIN);
}

bool board_power_15v_enabled(void) {
  return LL_GPIO_IsOutputPinSet(RF_15V_EN_PORT, 1UL << RF_15V_EN_PIN);
}

static void apply_power_requests(void) {
  bool need_15v = rf_power || manual_15v;
  bool need_5v = need_15v || lcd_power || manual_5v;

  /* Shut down in reverse order. Keep 5 V available for the LCD if requested. */
  if (!need_15v)
    LL_GPIO_ResetOutputPin(RF_15V_EN_PORT, 1UL << RF_15V_EN_PIN);
  if (!need_5v) {
    LL_GPIO_ResetOutputPin(RF_5V_EN_PORT, 1UL << RF_5V_EN_PIN);
    return;
  }
  if (!board_power_5v_enabled()) {
    LL_GPIO_SetOutputPin(RF_5V_EN_PORT, 1UL << RF_5V_EN_PIN);
    five_volt_started_ms = time_ms();
  }
  if (need_15v && !board_power_15v_enabled()) {
    uint32_t elapsed = time_elapsed_ms(five_volt_started_ms);
    if (elapsed < RF_RAIL_SETTLE_MS)
      time_delay_ms(RF_RAIL_SETTLE_MS - elapsed);
    LL_GPIO_SetOutputPin(RF_15V_EN_PORT, 1UL << RF_15V_EN_PIN);
    time_delay_ms(RF_RAIL_SETTLE_MS);
  }
}

void board_rf_power_on(void) {
  rf_power = true;
  apply_power_requests();
}

void board_rf_power_off(void) {
  rf_power = false;
  apply_power_requests();
}

void board_lcd_power_enable(bool enable) {
  lcd_power = enable;
  apply_power_requests();
}

void board_aux_power_enable(bool enable_5v, bool enable_15v) {
  manual_5v = enable_5v;
  manual_15v = enable_15v;
  apply_power_requests();
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
