#include "drivers/adf4158.h"
#include "board/board_pins.h"
#include "platform/gpio.h"

static void adf4158_delay(void) {
  uint32_t start = DWT->CYCCNT;

  while ((DWT->CYCCNT - start) < 48U) {
  }
}

void adf4158_init(void) {
  gpio_config_t freq_le = {
    .port = FREQ_LE_PORT,
    .pin = FREQ_LE_PIN,
    .mode = GPIO_MODE_OUTPUT,
    .output_type = GPIO_PUSH_PULL,
    .pull = GPIO_PULL_NONE,
    .speed = GPIO_SPEED_MEDIUM,
    .init_state = false,
  };

  gpio_config_t freq_clk = {
    .port = FREQ_CLK_PORT,
    .pin = FREQ_CLK_PIN,
    .mode = GPIO_MODE_OUTPUT,
    .output_type = GPIO_PUSH_PULL,
    .pull = GPIO_PULL_NONE,
    .speed = GPIO_SPEED_MEDIUM,
    .init_state = false,
  };

  gpio_config_t freq_data = {
    .port = FREQ_DATA_PORT,
    .pin = FREQ_DATA_PIN,
    .mode = GPIO_MODE_OUTPUT,
    .output_type = GPIO_PUSH_PULL,
    .pull = GPIO_PULL_NONE,
    .speed = GPIO_SPEED_MEDIUM,
    .init_state = false,
  };

  gpio_init(&freq_le);
  gpio_init(&freq_clk);
  gpio_init(&freq_data);
}

void adf4158_write(uint32_t value) {
  gpio_write(FREQ_LE_PORT, FREQ_LE_PIN, false);
  gpio_write(FREQ_CLK_PORT, FREQ_CLK_PIN, false);

  adf4158_delay();

  for (int bit = 31; bit >= 0; --bit) {
    gpio_write(FREQ_DATA_PORT, FREQ_DATA_PIN, ((value >> bit) & 1U) != 0U);

    adf4158_delay();

    gpio_write(FREQ_CLK_PORT, FREQ_CLK_PIN, true);

    adf4158_delay();

    gpio_write(FREQ_CLK_PORT, FREQ_CLK_PIN, false);

    adf4158_delay();
  }

  adf4158_delay();

  gpio_write(FREQ_LE_PORT, FREQ_LE_PIN, true);

  adf4158_delay();

  gpio_write(FREQ_LE_PORT, FREQ_LE_PIN, false);
}
