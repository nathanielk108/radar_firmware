#pragma once

#include "services/acquisition.h"

#define CHIRP_MAX_COUNT ACQUISITION_MAX_BURST_COUNT
#define CHIRP_MIN_REPETITION_US 500U
#define CHIRP_MAX_REPETITION_US 10000U

typedef struct {
  acquisition_block_t samples; /* First chirp and the capture sequence. */
  uint64_t start_hz;
  uint32_t sample_rate_hz;
  uint32_t step_rate_hz;
  uint32_t step_hz;
  uint32_t bandwidth_hz;
  uint32_t timer_hz;
  int32_t first_sample_offset_ticks; /* Nominal offset from first TXDATA rise. */
  uint32_t repetition_ticks;        /* Zero for a standalone chirp. */
  uint16_t chirp_count;
  uint16_t sample_stride; /* int16_t items between successive chirp views. */
  uint16_t gain_mv;        /* Nominal IF gain-control voltage for this capture. */
} chirp_capture_t;

/* Foreground only. Start makes one capture pending and returns immediately
 * after programming the PLL. Poll services its settle and acquisition phases.
 * ACQUISITION_BUSY means still in progress. Each result is returned once. */
bool chirp_capture_active(void);
acquisition_status_t chirp_capture_start(void);
acquisition_status_t chirp_capture_train_start(uint16_t chirp_count,
                                              uint32_t repetition_us);
acquisition_status_t chirp_capture_poll(chirp_capture_t *capture);
acquisition_status_t chirp_capture_cancel(void);

/* Get an aligned DMA view of one chirp. Its sequence identifies the entire
 * capture. Timestamp is nominal, derived from the timer start and repetition
 * interval. Views remain valid until the next ADC burst is armed. */
bool chirp_capture_get_block(const chirp_capture_t *capture, uint16_t index,
                             acquisition_block_t *block);

/* Run one board-profile chirp, waiting for settling and capture. */
acquisition_status_t chirp_capture_one(chirp_capture_t *capture);
