#include "board/board_freq.h"
#include "board/board_pins.h"
#include "platform/gpio.h"

void board_freq_init(void) {
    gpio_config_t le = {
        .port = FREQ_LE_PORT, .pin = FREQ_LE_PIN,
        .mode = GPIO_MODE_OUTPUT, .output_type = GPIO_PUSH_PULL,
        .pull = GPIO_PULL_NONE, .speed = GPIO_SPEED_MEDIUM,
        .init_state = false
    };

    gpio_config_t clk = {
        .port = FREQ_CLK_PORT, .pin = FREQ_CLK_PIN,
        .mode = GPIO_MODE_OUTPUT, .output_type = GPIO_PUSH_PULL,
        .pull = GPIO_PULL_NONE, .speed = GPIO_SPEED_MEDIUM,
        .init_state = false
    };

    gpio_config_t data = {
        .port = FREQ_DATA_PORT, .pin = FREQ_DATA_PIN,
        .mode = GPIO_MODE_OUTPUT, .output_type = GPIO_PUSH_PULL,
        .pull = GPIO_PULL_NONE, .speed = GPIO_SPEED_MEDIUM,
        .init_state = false
    };

    gpio_init(&le);
    gpio_init(&clk);
    gpio_init(&data);
}
