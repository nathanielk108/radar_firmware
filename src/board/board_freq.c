#include "board/board_freq.h"
#include "board/board_gpio.h"
#include "board/board_pins.h"
#include "drivers/adf4158.h"

const adf4158_ramp_config_t board_adf4158_ramp = {
    .pll = {.reference_hz = 10000000U,
            .frequency_hz = 5725000000ULL,
            .r_counter = 1U,
            .cp_current = 15U,
            .reference_doubler = false,
            .reference_div2 = false,
            .prescaler_8_9 = true,
            .positive_pd = true},
    .bandwidth_hz = 150000000U,
    .steps = 120U,
    .mode = ADF4158_RAMP_SINGLE,
    .external_step_hz = 800000U,
};

const adf4158_fixed_config_t board_adf4158_fixed = {
    .reference_hz = 10000000U,
    .frequency_hz = 5800000000ULL,
    .r_counter = 1U,
    .cp_current = 15U,
    .reference_doubler = false,
    .reference_div2 = false,
    .prescaler_8_9 = true,
    .positive_pd = true,
};

void board_freq_init(void) {
  board_gpio_pin(FREQ_LE_PORT, FREQ_LE_PIN, LL_GPIO_MODE_OUTPUT,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_MEDIUM, 0U, false);

  board_gpio_pin(FREQ_CLK_PORT, FREQ_CLK_PIN, LL_GPIO_MODE_OUTPUT,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_MEDIUM, 0U, false);

  board_gpio_pin(FREQ_DATA_PORT, FREQ_DATA_PIN, LL_GPIO_MODE_OUTPUT,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_MEDIUM, 0U, false);
  board_freq_hold_txdata_low();
}

void board_freq_hold_txdata_low(void) {
  board_gpio_pin(FREQ_TXDATA_PORT, FREQ_TXDATA_PIN, LL_GPIO_MODE_OUTPUT,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_LOW, 0U, false);
}

void board_freq_use_txdata_timer(void) {
  board_gpio_pin(FREQ_TXDATA_PORT, FREQ_TXDATA_PIN, LL_GPIO_MODE_ALTERNATE,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_HIGH, FREQ_TXDATA_AF, false);
}
