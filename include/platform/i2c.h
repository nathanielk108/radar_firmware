#pragma once

#include "stm32h7xx.h"

#include <stddef.h>
#include <stdint.h>

typedef enum {
    I2C_STATUS_OK = 0,
    I2C_STATUS_INVALID_ARGUMENT,
    I2C_STATUS_TIMEOUT,
    I2C_STATUS_NACK,
    I2C_STATUS_BUS_ERROR,
    I2C_STATUS_ARBITRATION_LOST,
    I2C_STATUS_OVERRUN
} i2c_status_t;

typedef struct {
    I2C_TypeDef *i2c;
    uint32_t timing;
} i2c_config_t;

i2c_status_t i2c_init(const i2c_config_t *config);

i2c_status_t i2c_write(
    const i2c_config_t *config,
    uint8_t address,
    const uint8_t *data,
    uint8_t length,
    uint32_t timeout_ms
);

i2c_status_t i2c_read(
    const i2c_config_t *config,
    uint8_t address,
    uint8_t *data,
    uint8_t length,
    uint32_t timeout_ms
);

i2c_status_t i2c_write_read(
    const i2c_config_t *config,
    uint8_t address,
    const uint8_t *tx,
    uint8_t tx_length,
    uint8_t *rx,
    uint8_t rx_length,
    uint32_t timeout_ms
);
