#pragma once

#include "stm32h7xx_ll_bus.h"
#include "stm32h7xx_ll_gpio.h"
#include <stdbool.h>
#include <stdint.h>

/* Board pin setup only. Runtime GPIO access uses LL_GPIO_* directly. */
static inline void board_gpio_clock(GPIO_TypeDef *port) {
  if (port == GPIOA)
    LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOA);
  else if (port == GPIOB)
    LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOB);
  else if (port == GPIOC)
    LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOC);
  else if (port == GPIOD)
    LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOD);
  else if (port == GPIOE)
    LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOE);
  else if (port == GPIOF)
    LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOF);
  else if (port == GPIOG)
    LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOG);
  else if (port == GPIOH)
    LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOH);
  else if (port == GPIOI)
    LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOI);
#ifdef GPIOJ
  else if (port == GPIOJ)
    LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOJ);
#endif
#ifdef GPIOK
  else if (port == GPIOK)
    LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOK);
#endif
}

static inline void board_gpio_pin(GPIO_TypeDef *port, uint8_t index,
                                  uint32_t mode, uint32_t type, uint32_t pull,
                                  uint32_t speed, uint32_t af, bool high) {
  const uint32_t pin = 1UL << index;
  board_gpio_clock(port);
  /* Set output level before enabling output mode (glitch-free rail/CE startup).
   */
  if (high)
    LL_GPIO_SetOutputPin(port, pin);
  else
    LL_GPIO_ResetOutputPin(port, pin);
  LL_GPIO_SetPinOutputType(port, pin, type);
  LL_GPIO_SetPinPull(port, pin, pull);
  LL_GPIO_SetPinSpeed(port, pin, speed);
  if (mode == LL_GPIO_MODE_ALTERNATE) {
    if (index < 8U)
      LL_GPIO_SetAFPin_0_7(port, pin, af);
    else
      LL_GPIO_SetAFPin_8_15(port, pin, af);
  }
  LL_GPIO_SetPinMode(port, pin, mode);
}
