#include "platform/gpio.h"

static void gpio_enable_clock(GPIO_TypeDef *port) {
  if (port == GPIOA) {
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOAEN;
  }
  else if (port == GPIOB) {
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOBEN;
  }
  else if (port == GPIOC) {
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOCEN;
  }
  else if (port == GPIOD) {
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIODEN;
  }
  else if (port == GPIOE) {
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOEEN;
  }
  else if (port == GPIOF) {
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOFEN;
  }
  else if (port == GPIOG) {
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOGEN;
  }
  else if (port == GPIOH) {
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOHEN;
  }
  else if (port == GPIOI) {
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOIEN;
  }
  else if (port == GPIOJ) {
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOJEN;
  }
  else if (port == GPIOK) {
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOKEN;
  }

  (void)RCC->AHB4ENR;
  (void)RCC->AHB4ENR;
}

void gpio_init(const gpio_config_t *config) {
  gpio_enable_clock(config->port);

  if (config->init_state) {
    config->port->BSRR = (1U << config->pin);
  }
  else {
    config->port->BSRR = (1U << config->pin) << 16U;
  }

  uint32_t shift = 2U * config->pin;
  config->port->MODER &= ~(0x3U << shift);
  config->port->MODER |= ((uint32_t)config->mode << shift);

  config->port->OTYPER &= ~(0x1U << config->pin);
  config->port->OTYPER |= ((uint32_t)config->output_type << config->pin);

  config->port->PUPDR &= ~(0x3U << shift);
  config->port->PUPDR |= ((uint32_t)config->pull << shift);

  config->port->OSPEEDR &= ~(0x3U << shift);
  config->port->OSPEEDR |= ((uint32_t)config->speed << shift);

  uint32_t index = config->pin / 8U;
  shift = (config->pin % 8U) * 4U;
  config->port->AFR[index] &= ~(0xFU << shift);
  config->port->AFR[index] |= ((uint32_t)config->alternate << shift);
}

void gpio_write(GPIO_TypeDef *port, uint8_t pin, bool state) {
  if (state) {
    port->BSRR = (1U << pin);
  }
  else {
    port->BSRR = (1U << (pin + 16U));
  }
}

bool gpio_read(GPIO_TypeDef *port, uint8_t pin) {
  return (port->IDR & (1U << pin)) != 0U;
}

void gpio_toggle(GPIO_TypeDef *port, uint8_t pin) {
  gpio_write(port, pin, !gpio_read(port, pin));
}
