#include "board/board_timer.h"

#include "board/board_freq.h"
#include "platform/time.h"
#include "stm32h7xx_ll_bus.h"
#include "stm32h7xx_ll_dma.h"
#include "stm32h7xx_ll_tim.h"

#define DMA_CHIRP_EDGES LL_DMA_STREAM_3
#define DMA_ADC_START LL_DMA_STREAM_4
#define DMA_CHIRP_EDGE_ERRORS                                                  \
  (DMA_LISR_TEIF3 | DMA_LISR_DMEIF3 | DMA_LISR_FEIF3)
#define DMA_ADC_START_ERRORS (DMA_HISR_TEIF4 | DMA_HISR_DMEIF4 | DMA_HISR_FEIF4)
#define DMA_BUFFER __attribute__((section(".dma_buffer"), aligned(32)))

DMA_BUFFER static uint32_t chirp_edges[2U * BOARD_CHIRP_MAX_STEPS - 1U];
DMA_BUFFER static uint32_t adc_start_cr1;

void board_timer_adc_init(void) {
  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_TIM16);
}

static bool valid_timing(const board_chirp_timing_t *timing) {
  if (!timing || !timing->step_count ||
      timing->step_count > BOARD_CHIRP_MAX_STEPS || !timing->adc_start_ticks ||
      timing->adc_start_ticks >= timing->first_step_ticks ||
      !timing->step_high_ticks ||
      timing->step_high_ticks >= timing->step_period_ticks)
    return false;

  uint64_t last_edge =
      (uint64_t)timing->first_step_ticks +
      (uint64_t)(timing->step_count - 1U) * timing->step_period_ticks +
      timing->step_high_ticks;
  return last_edge < timing->end_ticks;
}

static void configure_dma(uint32_t stream, uint32_t request,
                          const uint32_t *source, volatile uint32_t *target,
                          uint32_t count, uint32_t memory_increment) {
  LL_DMA_ConfigTransfer(DMA2, stream,
                        LL_DMA_DIRECTION_MEMORY_TO_PERIPH | LL_DMA_MODE_NORMAL |
                            LL_DMA_PRIORITY_HIGH | LL_DMA_PERIPH_NOINCREMENT |
                            LL_DMA_PDATAALIGN_WORD | LL_DMA_MDATAALIGN_WORD |
                            memory_increment);
  LL_DMA_ConfigAddresses(DMA2, stream, (uint32_t)(uintptr_t)source,
                         (uint32_t)(uintptr_t)target,
                         LL_DMA_DIRECTION_MEMORY_TO_PERIPH);
  LL_DMA_SetDataLength(DMA2, stream, count);
  LL_DMA_SetPeriphRequest(DMA2, stream, request);
  LL_DMA_EnableStream(DMA2, stream);
}

bool board_timer_chirp_prepare(const board_chirp_timing_t *timing) {
  if (!valid_timing(timing) || LL_TIM_IsEnabledCounter(TIM16) ||
      !(TIM16->CR1 & TIM_CR1_OPM))
    return false;

  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM2);
  LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA2);
  if (!board_timer_chirp_stop())
    return false;

  /* Reset CH1's toggle state while the pin is held low by GPIO. */
  LL_APB1_GRP1_ForceReset(LL_APB1_GRP1_PERIPH_TIM2);
  LL_APB1_GRP1_ReleaseReset(LL_APB1_GRP1_PERIPH_TIM2);
  TIM2->CR1 = TIM_CR1_OPM;
  TIM2->ARR = timing->end_ticks - 1U;
  TIM2->CCR1 = timing->first_step_ticks;
  TIM2->CCR2 = timing->adc_start_ticks;
  LL_TIM_OC_SetMode(TIM2, LL_TIM_CHANNEL_CH1, LL_TIM_OCMODE_TOGGLE);
  LL_TIM_CC_EnableChannel(TIM2, LL_TIM_CHANNEL_CH1 | LL_TIM_CHANNEL_CH2);
  LL_TIM_GenerateEvent_UPDATE(TIM2);
  TIM2->SR = 0U;

  /* CH1 toggles at CCR1. Each event loads the next absolute edge, so the
   * last falling edge leaves TXDATA low until TIM2 stops on its update.
   */
  uint32_t rise = timing->first_step_ticks;
  uint32_t count = 0U;
  for (uint32_t step = 0U; step < timing->step_count; ++step) {
    if (step)
      chirp_edges[count++] = rise;
    chirp_edges[count++] = rise + timing->step_high_ticks;
    rise += timing->step_period_ticks;
  }
  adc_start_cr1 = TIM16->CR1 | TIM_CR1_CEN;
  __DMB();
  configure_dma(DMA_CHIRP_EDGES, LL_DMAMUX1_REQ_TIM2_CH1, chirp_edges,
                &TIM2->CCR1, count, LL_DMA_MEMORY_INCREMENT);
  configure_dma(DMA_ADC_START, LL_DMAMUX1_REQ_TIM2_CH2, &adc_start_cr1,
                &TIM16->CR1, 1U, LL_DMA_MEMORY_NOINCREMENT);

  board_freq_use_txdata_timer();
  LL_TIM_EnableDMAReq_CC1(TIM2);
  LL_TIM_EnableDMAReq_CC2(TIM2);
  return true;
}

void board_timer_chirp_start(void) { LL_TIM_EnableCounter(TIM2); }

bool board_timer_chirp_complete(void) {
  return !LL_TIM_IsEnabledCounter(TIM2) && LL_TIM_IsActiveFlag_UPDATE(TIM2) &&
         LL_DMA_GetDataLength(DMA2, DMA_CHIRP_EDGES) == 0U &&
         LL_DMA_GetDataLength(DMA2, DMA_ADC_START) == 0U;
}

bool board_timer_chirp_has_error(void) {
  return (DMA2->LISR & DMA_CHIRP_EDGE_ERRORS) != 0U ||
         (DMA2->HISR & DMA_ADC_START_ERRORS) != 0U;
}

bool board_timer_chirp_stop(void) {
  LL_TIM_DisableDMAReq_CC1(TIM2);
  LL_TIM_DisableDMAReq_CC2(TIM2);
  LL_TIM_DisableCounter(TIM2);
  board_freq_hold_txdata_low();
  LL_DMA_DisableStream(DMA2, DMA_CHIRP_EDGES);
  LL_DMA_DisableStream(DMA2, DMA_ADC_START);

  uint32_t started = time_ms();
  while (LL_DMA_IsEnabledStream(DMA2, DMA_CHIRP_EDGES) ||
         LL_DMA_IsEnabledStream(DMA2, DMA_ADC_START)) {
    if (time_expired(started, 2U))
      return false;
  }
  DMA2->LIFCR = DMA_CHIRP_EDGE_ERRORS | DMA_LISR_TCIF3 | DMA_LISR_HTIF3;
  DMA2->HIFCR = DMA_ADC_START_ERRORS | DMA_HISR_TCIF4 | DMA_HISR_HTIF4;
  return true;
}
