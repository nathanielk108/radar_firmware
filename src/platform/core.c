#include "platform/core.h"
#include "stm32h7xx.h"

#define DWT_UNLOCK 0xC5ACCE55U

void core_enable_faults(void) {
  SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk |
                SCB_SHCSR_USGFAULTENA_Msk;
}

void core_enable_icache(void) { SCB_EnableICache(); }

void core_enable_dcache(void) { SCB_EnableDCache(); }

void core_enable_cycle_counter(void) {
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->LAR = DWT_UNLOCK;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}
