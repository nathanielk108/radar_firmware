#include "platform/time.h"
#include "stm32h7xx.h"

static volatile uint32_t system_ms;

bool time_init(void) {
  uint32_t ticks = SystemCoreClock / 1000U;

  if ((ticks == 0U) || ((ticks - 1U) > SysTick_LOAD_RELOAD_Msk)) {
    return false;
  }

  SysTick->LOAD = ticks - 1U;
  SysTick->VAL = 0U;

  NVIC_SetPriority(SysTick_IRQn, (1U << __NVIC_PRIO_BITS) - 1U);

  SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;

  return true;
}

void SysTick_Handler(void) {
  ++system_ms;
}

uint32_t time_ms(void) {
  return system_ms;
}

uint32_t time_elapsed_ms(uint32_t start) {
  return time_ms() - start;
}

bool time_expired(uint32_t start, uint32_t duration) {
  return time_elapsed_ms(start) >= duration;
}
