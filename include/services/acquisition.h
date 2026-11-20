#pragma once

#include <stdbool.h>
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
acquisition_status_t acquisition_deinit(void);

/* True while timed or burst capture owns the ADC SPI and CS configuration. */
bool acquisition_active(void);

/* Continuous timed acquisition. Each channel receives samples_per_block
 * valid 16-bit codes. A priming block is consumed before publishing any
 * samples, so the old conversion from the previous transaction is hidden.
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
#define ACQUISITION_MAX_BURST_COUNT 64U

typedef struct {
  uint32_t sample_rate_hz;
  uint16_t samples_per_block;
} acquisition_timed_config_t;

typedef struct {
  /* Read-only Q15 channel views into 32-byte-aligned DMA memory. */
  const int16_t *a;
  const int16_t *b;
  uint32_t sequence; /* Stream block or chirp capture identifier. */
  uint32_t first_sample_us; /* Nominal time_us() of the first valid conversion. */
  uint32_t sample_rate_hz;
  uint16_t count;
  bool timestamp_valid;
} acquisition_block_t;

/* Configure continuous capture. Exclusive with burst and polling ADC access.
 * Call acquisition_init() after configuring the ADC.
 */
acquisition_status_t
acquisition_timed_init(const acquisition_timed_config_t *config);
acquisition_status_t acquisition_timed_start(void);

/* Stop the timer first, then SPI and DMA. A published block remains readable
 * through get_block() until release, restart or deinit, provided no overrun
 * occurred. Call deinit before switching back to burst or polling mode.
 */
acquisition_status_t acquisition_timed_stop(void);

/* ACQUISITION_BUSY means no *new complete paired* block is available.
 * The views expire when DMA switches back to their target or at the next
 * start. Release each block before the next block completes. Commit any
 * processing result only after release succeeds. OVERRUN stops sampling and
 * invalidates the outstanding block. Stop and restart to resume the stream.
 * All stream control and buffer access are for foreground code.
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

/* Arm a train of bursts. Each external trigger starts one TIM16 pulse train.
 * stride is the spacing in int16_t samples between aligned blocks. */
acquisition_status_t acquisition_burst_train_arm(uint16_t samples,
                                                uint16_t bursts,
                                                uint16_t *stride);

/* Return aligned signed ADC views after both DMA streams and TIM16 finish.
 * For a train, the returned view describes the first block. Views are valid
 * until the next burst arm. Each block skips its stale word. Raw external
 * triggers have no software timestamp. Use the chirp service for timed RF.
 */
acquisition_status_t acquisition_burst_finish(acquisition_block_t *block);

/* Disarm the board's trigger DMA before aborting a burst. */
acquisition_status_t acquisition_burst_stop(void);

/* Capture a standalone block at 400 kSPS without triggering an RF sweep.
 * The returned views are valid until the next burst is armed.
 */
acquisition_status_t acquisition_capture_block(uint16_t samples,
                                              acquisition_block_t *block);
