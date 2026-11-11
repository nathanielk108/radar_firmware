#include "platform/i2c.h"
#include "platform/time.h"

static bool i2c_enable_clock(I2C_TypeDef *i2c)
{
    if (i2c == I2C1) RCC->APB1LENR |= RCC_APB1LENR_I2C1EN;
    else if (i2c == I2C2) RCC->APB1LENR |= RCC_APB1LENR_I2C2EN;
    else if (i2c == I2C3) RCC->APB1LENR |= RCC_APB1LENR_I2C3EN;
    else if (i2c == I2C4) RCC->APB4ENR |= RCC_APB4ENR_I2C4EN;
    else return false;
    (void)RCC->APB1LENR;
    (void)RCC->APB4ENR;
    return true;
}

static void i2c_clear_flags(I2C_TypeDef *i2c)
{
    i2c->ICR = I2C_ICR_STOPCF | I2C_ICR_NACKCF |
               I2C_ICR_BERRCF | I2C_ICR_ARLOCF | I2C_ICR_OVRCF;
}

static i2c_status_t i2c_error(I2C_TypeDef *i2c)
{
    uint32_t flags = i2c->ISR;
    if (flags & I2C_ISR_NACKF) return I2C_STATUS_NACK;
    if (flags & I2C_ISR_BERR) return I2C_STATUS_BUS_ERROR;
    if (flags & I2C_ISR_ARLO) return I2C_STATUS_ARBITRATION_LOST;
    if (flags & I2C_ISR_OVR) return I2C_STATUS_OVERRUN;
    return I2C_STATUS_OK;
}

/* Ensure a failed transaction does not leave the controller active.
   A slave physically holding SDA low still needs hardware-level recovery. */
static void i2c_abort(I2C_TypeDef *i2c)
{
    if ((i2c->ISR & I2C_ISR_BUSY) != 0U) {
        i2c->CR2 |= I2C_CR2_STOP;
        uint32_t start = time_ms();
        while (((i2c->ISR & I2C_ISR_STOPF) == 0U) &&
               (time_elapsed_ms(start) < 2U)) {}
    }
    i2c->CR1 &= ~I2C_CR1_PE;
    i2c_clear_flags(i2c);
    i2c->CR2 = 0U;
    i2c->CR1 |= I2C_CR1_PE;
}

static i2c_status_t i2c_wait(I2C_TypeDef *i2c, uint32_t flag,
                             uint32_t start, uint32_t timeout_ms)
{
    for (;;) {
        i2c_status_t error = i2c_error(i2c);
        if (error != I2C_STATUS_OK) return error;
        if ((i2c->ISR & flag) != 0U) return I2C_STATUS_OK;
        if (time_elapsed_ms(start) >= timeout_ms) return I2C_STATUS_TIMEOUT;
    }
}

static i2c_status_t i2c_begin(const i2c_config_t *config, uint8_t address,
                               uint32_t start, uint32_t timeout_ms)
{
    if ((config == NULL) || (config->i2c == NULL) || (address > 0x7FU) ||
        (timeout_ms == 0U)) return I2C_STATUS_INVALID_ARGUMENT;

    I2C_TypeDef *i2c = config->i2c;
    if ((i2c->CR1 & I2C_CR1_PE) == 0U) return I2C_STATUS_INVALID_ARGUMENT;
    while ((i2c->ISR & I2C_ISR_BUSY) != 0U) {
        if (time_elapsed_ms(start) >= timeout_ms) return I2C_STATUS_TIMEOUT;
    }
    i2c_clear_flags(i2c);
    return I2C_STATUS_OK;
}

i2c_status_t i2c_init(const i2c_config_t *config)
{
    if ((config == NULL) || (config->i2c == NULL) ||
        !i2c_enable_clock(config->i2c)) return I2C_STATUS_INVALID_ARGUMENT;

    I2C_TypeDef *i2c = config->i2c;
    i2c->CR1 &= ~I2C_CR1_PE;
    i2c->CR1 = 0U;
    i2c->CR2 = 0U;
    i2c->TIMINGR = config->timing;
    i2c_clear_flags(i2c);
    i2c->CR1 |= I2C_CR1_PE;
    return I2C_STATUS_OK;
}

i2c_status_t i2c_write(const i2c_config_t *config, uint8_t address,
                       const uint8_t *data, uint8_t length, uint32_t timeout_ms)
{
    if ((data == NULL) || (length == 0U)) return I2C_STATUS_INVALID_ARGUMENT;
    uint32_t start = time_ms();
    i2c_status_t status = i2c_begin(config, address, start, timeout_ms);
    if (status != I2C_STATUS_OK) return status;
    I2C_TypeDef *i2c = config->i2c;

    i2c->CR2 = ((uint32_t)address << 1U) |
               ((uint32_t)length << I2C_CR2_NBYTES_Pos) |
               I2C_CR2_AUTOEND | I2C_CR2_START;

    for (uint8_t i = 0; i < length; ++i) {
        status = i2c_wait(i2c, I2C_ISR_TXIS, start, timeout_ms);
        if (status != I2C_STATUS_OK) goto failed;
        i2c->TXDR = data[i];
    }
    status = i2c_wait(i2c, I2C_ISR_STOPF, start, timeout_ms);
    if (status != I2C_STATUS_OK) goto failed;
    i2c_clear_flags(i2c);
    return I2C_STATUS_OK;
failed:
    i2c_abort(i2c);
    return status;
}

i2c_status_t i2c_read(const i2c_config_t *config, uint8_t address,
                      uint8_t *data, uint8_t length, uint32_t timeout_ms)
{
    if ((data == NULL) || (length == 0U)) return I2C_STATUS_INVALID_ARGUMENT;
    uint32_t start = time_ms();
    i2c_status_t status = i2c_begin(config, address, start, timeout_ms);
    if (status != I2C_STATUS_OK) return status;
    I2C_TypeDef *i2c = config->i2c;

    i2c->CR2 = ((uint32_t)address << 1U) |
               ((uint32_t)length << I2C_CR2_NBYTES_Pos) |
               I2C_CR2_RD_WRN | I2C_CR2_AUTOEND | I2C_CR2_START;
    for (uint8_t i = 0; i < length; ++i) {
        status = i2c_wait(i2c, I2C_ISR_RXNE, start, timeout_ms);
        if (status != I2C_STATUS_OK) goto failed;
        data[i] = (uint8_t)i2c->RXDR;
    }
    status = i2c_wait(i2c, I2C_ISR_STOPF, start, timeout_ms);
    if (status != I2C_STATUS_OK) goto failed;
    i2c_clear_flags(i2c);
    return I2C_STATUS_OK;
failed:
    i2c_abort(i2c);
    return status;
}

i2c_status_t i2c_write_read(const i2c_config_t *config, uint8_t address,
                            const uint8_t *tx, uint8_t tx_length,
                            uint8_t *rx, uint8_t rx_length, uint32_t timeout_ms)
{
    if ((tx == NULL) || (rx == NULL) || (tx_length == 0U) ||
        (rx_length == 0U)) return I2C_STATUS_INVALID_ARGUMENT;
    uint32_t start = time_ms();
    i2c_status_t status = i2c_begin(config, address, start, timeout_ms);
    if (status != I2C_STATUS_OK) return status;
    I2C_TypeDef *i2c = config->i2c;

    /* No AUTOEND in write phase; TC permits repeated START. */
    i2c->CR2 = ((uint32_t)address << 1U) |
               ((uint32_t)tx_length << I2C_CR2_NBYTES_Pos) | I2C_CR2_START;
    for (uint8_t i = 0; i < tx_length; ++i) {
        status = i2c_wait(i2c, I2C_ISR_TXIS, start, timeout_ms);
        if (status != I2C_STATUS_OK) goto failed;
        i2c->TXDR = tx[i];
    }
    status = i2c_wait(i2c, I2C_ISR_TC, start, timeout_ms);
    if (status != I2C_STATUS_OK) goto failed;

    i2c->CR2 = ((uint32_t)address << 1U) |
               ((uint32_t)rx_length << I2C_CR2_NBYTES_Pos) |
               I2C_CR2_RD_WRN | I2C_CR2_AUTOEND | I2C_CR2_START;
    for (uint8_t i = 0; i < rx_length; ++i) {
        status = i2c_wait(i2c, I2C_ISR_RXNE, start, timeout_ms);
        if (status != I2C_STATUS_OK) goto failed;
        rx[i] = (uint8_t)i2c->RXDR;
    }
    status = i2c_wait(i2c, I2C_ISR_STOPF, start, timeout_ms);
    if (status != I2C_STATUS_OK) goto failed;
    i2c_clear_flags(i2c);
    return I2C_STATUS_OK;
failed:
    i2c_abort(i2c);
    return status;
}

