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

/* Run one board-profile chirp. All sample views are read-only and valid until
 * the next ADC burst is armed. For train index i, each channel begins at
 * samples.a/b + i * sample_stride and has samples.count samples. */
acquisition_status_t chirp_capture_one(chirp_capture_t *capture);
