#pragma once

#include "platform/spi.h"
#include <stdint.h>

#define BMI323_CHIP_ID_REG  0x00U
#define BMI323_CHIP_ID_VAL  0x0043U

/* Requires board_spi_imu2_init() and spi_init(&imu2_spi_config). */
spi_status_t bmi323_read_id(uint16_t *id);

