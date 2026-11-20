#include "platform/time.h"

#include "platform/clock.h"
#include "stm32h7xx.h"
#include "stm32h7xx_ll_bus.h"

static volatile uint32_t system_ms;

_Static_assert(PLATFORM_APB_TIMER_HZ % 1000000U == 0U,
               "TIM5 must divide evenly to 1 MHz");

bool time_init(void) {
  uint32_t ticks = SystemCoreClock / 1000U;

  if ((ticks == 0U) || ((ticks - 1U) > SysTick_LOAD_RELOAD_Msk)) {
    return false;
  }

  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM5);
  LL_APB1_GRP1_ForceReset(LL_APB1_GRP1_PERIPH_TIM5);
  LL_APB1_GRP1_ReleaseReset(LL_APB1_GRP1_PERIPH_TIM5);
  TIM5->PSC = PLATFORM_APB_TIMER_HZ / 1000000U - 1U;
  TIM5->ARR = UINT32_MAX;
  TIM5->EGR = TIM_EGR_UG;
  TIM5->SR = 0U;
  TIM5->CR1 = TIM_CR1_CEN;

  SysTick->LOAD = ticks - 1U;
  SysTick->VAL = 0U;

  NVIC_SetPriority(SysTick_IRQn, (1U << __NVIC_PRIO_BITS) - 1U);

  SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk |
                  SysTick_CTRL_ENABLE_Msk;

  return true;
}

void SysTick_Handler(void) { ++system_ms; }

uint32_t time_ms(void) { return system_ms; }

uint32_t time_us(void) { return TIM5->CNT; }

uint32_t time_elapsed_ms(uint32_t start) { return time_ms() - start; }

bool time_expired(uint32_t start, uint32_t duration) {
  return time_elapsed_ms(start) >= duration;
}

void time_delay_ms(uint32_t duration) {
  const uint32_t start = time_ms();
  while (!time_expired(start, duration)) {
    __NOP();
  }
}

void time_delay_us(uint32_t duration) {
  const uint32_t cycles = (SystemCoreClock / 1000000U) * duration;
  const uint32_t start = DWT->CYCCNT;
  while ((uint32_t)(DWT->CYCCNT - start) < cycles) {
    __NOP();
  }
}
