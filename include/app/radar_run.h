#pragma once

#include "services/acquisition.h"
#include "services/storage.h"

#define RADAR_RUN_TAG_LENGTH 16U
#define RADAR_RUN_MAX_CAPTURES 1000U
#define RADAR_RUN_MAX_GAP_MS 60000U

typedef enum { RADAR_RUN_OUTPUT_SD, RADAR_RUN_OUTPUT_USB } radar_run_output_t;

typedef struct {
  char tag[RADAR_RUN_TAG_LENGTH + 1U];
  uint16_t captures;
  uint16_t chirps;
  uint32_t interval_us; /* Zero for single chirps. */
  uint32_t gap_ms;      /* Delay after each capture is written or queued. */
  radar_run_output_t output;
} radar_run_config_t;

typedef enum {
  RADAR_RUN_IDLE,
  RADAR_RUN_RUNNING,
  RADAR_RUN_COMPLETE,
  RADAR_RUN_STOPPED,
  RADAR_RUN_CAPTURE_ERROR,
  RADAR_RUN_STORAGE_ERROR,
  RADAR_RUN_USB_ERROR
} radar_run_state_t;

typedef enum {
  RADAR_RUN_START_OK,
  RADAR_RUN_START_BUSY,
  RADAR_RUN_START_INVALID,
  RADAR_RUN_START_RF_UNAVAILABLE,
  RADAR_RUN_START_SD_UNAVAILABLE,
  RADAR_RUN_START_USB_UNAVAILABLE
} radar_run_start_result_t;

typedef struct {
  radar_run_state_t state;
  radar_run_output_t output;
  uint16_t captures;
  uint16_t saved; /* Saved to SD or queued for USB, depending on output. */
  char path[13]; /* Run manifest, set after its header is saved. */
  acquisition_status_t capture_error;
  storage_status_t storage_error;
} radar_run_status_t;

/* Foreground only. While running, the application owns chirp acquisition
 * and RF settings. Schedule poll regularly. SD I/O can wait for the card.
 */
radar_run_start_result_t radar_run_start(const radar_run_config_t *config);
void radar_run_poll(void);
bool radar_run_stop(void);
radar_run_status_t radar_run_status(void);
const char *radar_run_state_name(radar_run_state_t state);
