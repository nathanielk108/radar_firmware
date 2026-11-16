#pragma once

#include <stdint.h>

typedef enum {
  BMI323_OK = 0,
  BMI323_ERROR_INVALID_ARGUMENT,
  BMI323_ERROR_TIMEOUT,
  BMI323_ERROR_OVERRUN,
  BMI323_ERROR_MODE_FAULT,
  BMI323_ERROR_SPI,
  BMI323_ERROR_DEVICE_ID,
} bmi323_status_t;

const char *bmi323_status_name(bmi323_status_t status);

#define BMI323_CHIP_ID_REG 0x00U
#define BMI323_CHIP_ID_VAL 0x0043U

/* Requires board_spi_imu2_init() and spi_init(&imu2_spi_config). */
bmi323_status_t bmi323_init(void);
bmi323_status_t bmi323_read_id(uint16_t *id);
