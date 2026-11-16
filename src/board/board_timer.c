#include "board/board_timer.h"

#include "stm32h7xx_ll_bus.h"

/* TIM16 CH1 drives ADC CS and the SPI5 TX DMA request. */
void board_timer_adc_init(void) {
  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_TIM16);
}
