#pragma once

#include "drivers/ad4683.h"

typedef enum {
  ACQUISITION_OK,
  ACQUISITION_BUSY,
  ACQUISITION_TIMEOUT,
  ACQUISITION_DMA_ERROR,
  ACQUISITION_SPI_ERROR
} acquisition_status_t;

acquisition_status_t acquisition_init(void);
acquisition_status_t acquisition_capture_one(ad4683_sample_t *sample,
                                             uint32_t timeout_ms);
