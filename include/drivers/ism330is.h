#pragma once

#include "platform/spi.h"
#include <stdint.h>

#define ISM330IS_WHO_AM_I_REG  0x0FU
#define ISM330IS_WHO_AM_I_VAL  0x22U

/* Requires board_spi_imu1_init() and spi_init(&imu1_spi_config). */
spi_status_t ism330is_read_id(uint8_t *id);

