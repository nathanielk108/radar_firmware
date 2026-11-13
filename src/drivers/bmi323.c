#include "drivers/bmi323.h"

#include "board/board_pins.h"
#include "board/board_spi.h"
#include "stm32h7xx_ll_gpio.h"

static spi_status_t bmi323_read_reg(uint8_t reg, uint16_t *value)
{
    if (value == NULL) {
        return SPI_STATUS_INVALID_ARGUMENT;
    }

    /* Address, dummy byte, little-endian 16-bit register value. */
    const uint8_t tx[4] = { (uint8_t)(0x80U | reg), 0U, 0U, 0U };
    uint8_t rx[4] = { 0U, 0U, 0U, 0U };

    LL_GPIO_ResetOutputPin(IMU2_CS_PORT, 1UL << IMU2_CS_PIN);
    spi_status_t status = spi_transfer(&imu2_spi_config, tx, rx, 4U, 20U);
    LL_GPIO_SetOutputPin(IMU2_CS_PORT, 1UL << IMU2_CS_PIN);

    if (status == SPI_STATUS_OK) {
        *value = (uint16_t)rx[2] | ((uint16_t)rx[3] << 8);
    }
    return status;
}

spi_status_t bmi323_read_id(uint16_t *id)
{
    if (id == NULL) {
        return SPI_STATUS_INVALID_ARGUMENT;
    }

    /* First transaction after power-on selects SPI interface. */
    uint16_t discard;
    spi_status_t status = bmi323_read_reg(BMI323_CHIP_ID_REG, &discard);
    if (status != SPI_STATUS_OK) {
        return status;
    }
    return bmi323_read_reg(BMI323_CHIP_ID_REG, id);
}

