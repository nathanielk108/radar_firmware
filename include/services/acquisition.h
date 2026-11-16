#pragma once

#include <stdint.h>

typedef enum {
  ACQUISITION_OK = 0,
  ACQUISITION_BUSY,
  ACQUISITION_TIMEOUT,
  ACQUISITION_DMA_ERROR,
  ACQUISITION_SPI_ERROR,
  ACQUISITION_INVALID_ARGUMENT,
  ACQUISITION_NOT_READY,
  ACQUISITION_OVERRUN
} acquisition_status_t;

const char *acquisition_status_name(acquisition_status_t status);

/* Call from the foreground after polling ADC register configuration. */
acquisition_status_t acquisition_init(void);

/* Continuous timed acquisition. Each channel receives samples_per_block
 * 16-bit codes. The first code pair is from the preceding ADC transaction
 * because every CS pulse returns the previous conversion.
 * The timer clock is 240 MHz and the SPI5 kernel clock is 120 MHz under
 * the firmware's existing clock configuration. Supported rates must divide
 * 240 MHz exactly; the design target is 400 kSPS.
 */
#define ACQUISITION_TIMER_HZ 240000000U
#define ACQUISITION_SAMPLE_RATE_HZ 400000U
#define ACQUISITION_TIMER_TICKS                                                \
  (ACQUISITION_TIMER_HZ / ACQUISITION_SAMPLE_RATE_HZ)
#define ACQUISITION_CS_OFFSET_TICKS 384U
#define ACQUISITION_MAX_BLOCK_SAMPLES 128U

typedef struct {
  uint32_t sample_rate_hz;
  uint16_t samples_per_block;
} acquisition_timed_config_t;

typedef struct {
  const int16_t *a;
  const int16_t *b;
  uint16_t count;
  uint32_t sequence;
} acquisition_block_t;

/* Configure continuous capture. Exclusive with burst and polling ADC access.
 * Call acquisition_init() after configuring the ADC.
 */
acquisition_status_t
acquisition_timed_init(const acquisition_timed_config_t *config);
acquisition_status_t acquisition_timed_start(void);

/* Stop the timer first, then SPI and DMA. A fully completed block remains
 * readable through acquisition_timed_get_block() until the next start/init.
 */
acquisition_status_t acquisition_timed_stop(void);

/* ACQUISITION_BUSY means no *new complete paired* block is available.
 * The views expire when DMA switches back to their target or at the next
 * start. Release each block before the next block completes. If DMA catches
 * up first, OVERRUN stops sampling and invalidates the outstanding block.
 */
acquisition_status_t acquisition_timed_get_block(acquisition_block_t *block);
acquisition_status_t acquisition_timed_release_block(uint32_t sequence);

/* Return PF6 to GPIO CS and restore polling SPI settings. */
acquisition_status_t acquisition_timed_deinit(void);

/* Arm a finite 400 kSPS burst and leave TIM16 stopped in one-pulse mode.
 * The board timing DMA starts it externally. TIM16 generates samples + 1
 * frames and stops after the pipeline-drain frame.
 */
acquisition_status_t acquisition_burst_arm(uint16_t samples);

/* Return aligned signed ADC views after both DMA streams and TIM16 finish.
 * Views are valid until the next burst arm. This call skips the stale word.
 */
acquisition_status_t acquisition_burst_finish(acquisition_block_t *block);

/* Disarm the board's trigger DMA before aborting a burst. */
acquisition_status_t acquisition_burst_stop(void);
