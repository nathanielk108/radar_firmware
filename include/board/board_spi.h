#pragma once

#include "platform/spi.h"

extern const spi_config_t imu1_spi_config;
extern const spi_config_t imu2_spi_config;
extern const spi_config_t adc_master_spi_config;
extern const spi_config_t adc_slave_spi_config;

void board_spi_imu1_init(void);
void board_spi_imu2_init(void);
void board_spi_adc_init(void);

// Initialize all SPI peripheral GPIO pins
void board_spi_init(void);
