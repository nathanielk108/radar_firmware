#pragma once

#include "services/acquisition.h"

typedef struct {
  acquisition_block_t samples;
  uint32_t sample_rate_hz;
  uint32_t step_rate_hz;
  uint32_t step_hz;
} chirp_capture_t;

/* Run one board-profile chirp. The sample views last until the next capture. */
acquisition_status_t chirp_capture_one(chirp_capture_t *capture);
