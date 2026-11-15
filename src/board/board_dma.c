#include "board/board_dma.h"

#include "stm32h7xx_ll_bus.h"
#include "stm32h7xx.h"

void board_dma_adc_init(void) {
  LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA2);

  /* Same preemption priority: ADC DMA ISRs update one event word. */
  NVIC_SetPriority(DMA2_Stream0_IRQn, 4U);
  NVIC_SetPriority(DMA2_Stream1_IRQn, 4U);
  NVIC_SetPriority(DMA2_Stream2_IRQn, 4U);
}

void board_dma_adc_enable_irqs(void) {
  NVIC_ClearPendingIRQ(DMA2_Stream0_IRQn);
  NVIC_ClearPendingIRQ(DMA2_Stream1_IRQn);
  NVIC_ClearPendingIRQ(DMA2_Stream2_IRQn);
  NVIC_EnableIRQ(DMA2_Stream0_IRQn);
  NVIC_EnableIRQ(DMA2_Stream1_IRQn);
  NVIC_EnableIRQ(DMA2_Stream2_IRQn);
}
