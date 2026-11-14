#pragma once
#include <stddef.h>

#include "platform/spi.h"
#include <stdint.h>

typedef enum {
  AD4683_OK = 0,
  AD4683_ERROR_INVALID_ARGUMENT,
  AD4683_ERROR_TIMEOUT,
  AD4683_ERROR_OVERRUN,
  AD4683_ERROR_MODE_FAULT,
  AD4683_ERROR_SPI,
} ad4683_status_t;

const char *ad4683_status_name(ad4683_status_t status);

typedef struct {
  int16_t a;
  int16_t b;
} ad4683_sample_t;

ad4683_status_t ad4683_init(void);
ad4683_status_t ad4683_read_sample(ad4683_sample_t *sample);

/* AD4683 16-bit command transaction; returns simultaneous previous results. */
ad4683_status_t ad4683_transfer(uint16_t command, uint16_t *a, uint16_t *b);

/* Returns the caller-owned buffer; do not call from an ISR. */
const char *ad4683_diagnostic(char *buf, size_t size);
