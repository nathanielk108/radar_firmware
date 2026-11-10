#pragma once

#include "stm32h7xx.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    SPI_STATUS_OK = 0,
    SPI_STATUS_INVALID_ARGUMENT,
    SPI_STATUS_TIMEOUT,
    SPI_STATUS_OVERRUN,
    SPI_STATUS_MODE_FAULT
} spi_status_t;

typedef enum {
    SPI_ROLE_SLAVE  = 0,
    SPI_ROLE_MASTER = 1
} spi_role_t;

typedef enum {
    SPI_COMM_FULL_DUPLEX = 0,
    SPI_COMM_TX_ONLY     = 1,
    SPI_COMM_RX_ONLY     = 2,
    SPI_COMM_HALF_DUPLEX = 3
} spi_comm_t;

typedef enum {
    SPI_MODE_0 = 0,
    SPI_MODE_1,
    SPI_MODE_2,
    SPI_MODE_3
} spi_mode_t;

typedef enum {
    SPI_MSB_FIRST = 0,
    SPI_LSB_FIRST = 1
} spi_bit_order_t;

typedef enum {
    SPI_DIV_2 = 0,
    SPI_DIV_4,
    SPI_DIV_8,
    SPI_DIV_16,
    SPI_DIV_32,
    SPI_DIV_64,
    SPI_DIV_128,
    SPI_DIV_256
} spi_baud_div_t;

typedef struct {
    SPI_TypeDef *spi;

    spi_role_t role;
    spi_comm_t communication;
    spi_mode_t mode;
    spi_bit_order_t bit_order;

    spi_baud_div_t baud_divider;
    uint8_t data_size;

    bool software_nss;
} spi_config_t;

spi_status_t spi_init(spi_config_t *config);

spi_status_t spi_transfer(const spi_config_t *config, const void *tx, void *rx, uint16_t frame_count, uint32_t timeout_ms);
