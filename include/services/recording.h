#pragma once

#include "services/chirp.h"
#include "services/storage.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RECORDING_FILENAME_BYTES 13U
#define RECORDING_HEADER_WORDS 14U

/* Serialize the little-endian RDR1 header for a completed capture. */
void recording_encode_header(const chirp_capture_t *capture,
                             uint32_t header[RECORDING_HEADER_WORDS]);

typedef struct {
  uint32_t captures_saved;
  uint32_t captures_dropped;
  uint32_t diagnostic_lines_dropped;
  uint32_t write_errors;
  uint8_t captures_pending;
  storage_status_t last_error;
  char last_capture[RECORDING_FILENAME_BYTES];
} recording_status_t;

/* Foreground only. Copy a finished capture into the SD queue while its
 * ADC view is valid. */
bool recording_capture(const chirp_capture_t *capture);

/* Foreground only, while capture is idle. Save a completed ADC view using
 * one free staging slot. A successful call closes the file and returns its
 * name in path. BUSY can be retried while the view remains valid. */
storage_status_t recording_save_capture(const chirp_capture_t *capture,
                                        char path[RECORDING_FILENAME_BYTES]);

void recording_note(const char *text, size_t length);
recording_status_t recording_status(void);

/* Foreground only. Schedule after time-critical work; file I/O may wait for
 * the card and runs only while capture is idle. */
void recording_task(void);
storage_status_t recording_flush(void);
