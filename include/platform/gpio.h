#pragma once

#include "stm32h7xx.h"

#include <stdint.h>
#include <stdbool.h>

typedef enum {
  GPIO_MODE_INPUT     = 0U,
  GPIO_MODE_OUTPUT    = 1U,
  GPIO_MODE_ALTERNATE = 2U,
  GPIO_MODE_ANALOG    = 3U,
} gpio_mode_t;

typedef enum {
  GPIO_PUSH_PULL  = 0U,
  GPIO_OPEN_DRAIN = 1U,
} gpio_output_type_t;

typedef enum {
  GPIO_PULL_NONE = 0U,
  GPIO_PULL_UP   = 1U,
  GPIO_PULL_DOWN = 2U,
} gpio_pull_t;

typedef enum {
  GPIO_SPEED_LOW       = 0U,
  GPIO_SPEED_MEDIUM    = 1U,
  GPIO_SPEED_HIGH      = 2U,
  GPIO_SPEED_VERY_HIGH = 3U,
} gpio_speed_t;

typedef struct {
  GPIO_TypeDef *port;
  uint8_t pin;

  gpio_mode_t mode;
  gpio_output_type_t output_type;
  gpio_pull_t pull;
  gpio_speed_t speed;

  uint8_t alternate;
  bool init_state;
} gpio_config_t;

void gpio_init(const gpio_config_t *config);

void gpio_write(GPIO_TypeDef *port, uint8_t pin, bool state);
bool gpio_read(GPIO_TypeDef *port, uint8_t pin);
void gpio_toggle(GPIO_TypeDef *port, uint8_t pin);
