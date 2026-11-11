#pragma once

#include "platform/spi.h"

extern const spi_config_t imu1_spi_config;
extern const spi_config_t imu2_spi_config;

void board_spi_init(void);
