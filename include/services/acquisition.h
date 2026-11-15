#pragma once

#include "drivers/ad4683.h"

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

/* The ADC driver performs register access with polling before DMA starts. */
acquisition_status_t acquisition_init(void);

/* Synchronous one-pair diagnostic; returns the previous ADC conversion. */
acquisition_status_t acquisition_capture_one(ad4683_sample_t *sample,
                                             uint32_t timeout_ms);

/* True while either acquisition method owns the SPI4/SPI5 bus. */
bool acquisition_is_busy(void);

/* Timed acquisition is continuous: no chirp or frame scheduling yet.
 * Each channel receives the same samples_per_block number of 16-bit codes.
 * The timer clock is 240 MHz and the SPI5 kernel clock is 120 MHz under
 * the firmware's existing clock configuration. Supported rates must divide
 * 240 MHz exactly; max tested design target is 400 kSPS.
 */
#define ACQUISITION_MAX_BLOCK_SAMPLES 128U

typedef struct {
  uint32_t sample_rate_hz;
  uint16_t samples_per_block;
} acquisition_timed_config_t;

typedef struct {
  const uint16_t *a;
  const uint16_t *b;
  uint16_t count;
  uint32_t sequence;
} acquisition_block_t;

/* Configure timer, CS pin mux, SPI and three DMA2 streams. Exclusive with
 * acquisition_capture_one() and all polling ADC register/diagnostic access.
 * Call after acquisition_init() and ad4683_init().
 */
acquisition_status_t acquisition_timed_init(
  const acquisition_timed_config_t *config);
acquisition_status_t acquisition_timed_start(void);

/* Stop the timer first, then SPI and DMA. A fully completed block remains
 * readable through acquisition_timed_get_block() until the next start/init.
 */
acquisition_status_t acquisition_timed_stop(void);

/* ACQUISITION_BUSY means no *new complete paired* block is available.
 * Buffer pointers are borrowed: release promptly, before DMA reuses them.
 * No copying is performed. A missed deadline stops sampling with OVERRUN.
 */
acquisition_status_t acquisition_timed_get_block(acquisition_block_t *block);
acquisition_status_t acquisition_timed_release_block(uint32_t sequence);

/* Return PF6 to GPIO CS and restore the working single-sample SPI/DMA setup. */
acquisition_status_t acquisition_timed_deinit(void);

const char *acquisition_diagnostic(char *buf, size_t size);
