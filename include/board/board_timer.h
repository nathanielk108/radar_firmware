#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BOARD_CHIRP_MAX_STEPS 256U
#define BOARD_CHIRP_MAX_COUNT 16U

typedef struct {
  uint32_t adc_start_ticks;
  uint32_t first_step_ticks;
  uint32_t step_period_ticks;
  uint32_t step_high_ticks;
  uint32_t end_ticks;
  uint32_t reset_ticks;
  uint16_t step_count;
  uint16_t chirp_count;
} board_chirp_timing_t;

void board_timer_adc_init(void);

/* TIM2 and TIM16 both run at 240 MHz. Arm TIM16 in one-pulse mode first;
 * TIM2's CH2 compare uses DMA to set TIM16 CEN at adc_start_ticks.
 * A train repeats the pulse pattern every end_ticks and adds one TXDATA
 * reset pulse at reset_ticks. CH3 sets one-pulse mode in the last interval.
 */
bool board_timer_chirp_prepare(const board_chirp_timing_t *timing);
void board_timer_chirp_start(void);
bool board_timer_chirp_complete(void);
bool board_timer_chirp_has_error(void);

/* Stop and disarm TIM2 and its DMA streams before stopping TIM16. */
bool board_timer_chirp_stop(void);
