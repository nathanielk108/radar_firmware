#pragma once

#include "platform/spi.h"

static spi_config_t imu1_spi_config = {
  .spi = SPI1,
  .role = SPI_ROLE_MASTER,
  .communication = SPI_COMM_FULL_DUPLEX,
  .mode = SPI_MODE_0,
  .bit_order = SPI_MSB_FIRST,
  .baud_divider = SPI_DIV_16,
  .data_size = 8U,
  .software_nss = true,
};

static spi_config_t imu2_spi_config = {
  .spi = SPI2,
  .role = SPI_ROLE_MASTER,
  .communication = SPI_COMM_FULL_DUPLEX,
  .mode = SPI_MODE_0,
  .bit_order = SPI_MSB_FIRST,
  .baud_divider = SPI_DIV_16,
  .data_size = 8U,
  .software_nss = true,
};

void board_spi_init(void);
