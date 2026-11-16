#pragma once

#include "services/chirp.h"
#include "services/storage.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint32_t captures_saved;
  uint32_t captures_dropped;
  uint32_t diagnostic_lines_dropped;
  uint32_t write_errors;
  uint8_t captures_pending;
  storage_status_t last_error;
  char last_capture[13];
} recording_status_t;

/* Foreground only. Enqueue before releasing or reusing the ADC buffer. */
bool recording_capture(const chirp_capture_t *capture, uint16_t gain_mv);
void recording_note(const char *text, size_t length);
recording_status_t recording_status(void);

/* Foreground only. Schedule after time-critical work; file I/O may wait for
 * the card and runs only while capture is idle. */
void recording_task(void);
storage_status_t recording_flush(void);
