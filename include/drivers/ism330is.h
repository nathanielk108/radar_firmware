#pragma once
#include <stddef.h>

#include "platform/spi.h"
#include <stdint.h>

typedef enum {
  ISM330IS_OK = 0,
  ISM330IS_ERROR_INVALID_ARGUMENT,
  ISM330IS_ERROR_TIMEOUT,
  ISM330IS_ERROR_OVERRUN,
  ISM330IS_ERROR_MODE_FAULT,
  ISM330IS_ERROR_SPI,
  ISM330IS_ERROR_DEVICE_ID,
} ism330is_status_t;

const char *ism330is_status_name(ism330is_status_t status);

#define ISM330IS_WHO_AM_I_REG 0x0FU
#define ISM330IS_WHO_AM_I_VAL 0x22U

/* Requires board_spi_imu1_init() and spi_init(&imu1_spi_config). */
ism330is_status_t ism330is_read_id(uint8_t *id);

/* Returns the caller-owned buffer; do not call from an ISR. */
const char *ism330is_diagnostic(char *buf, size_t size);

ism330is_status_t ism330is_init(void);
