#include "drivers/ism330is.h"

#include "board/board_pins.h"
#include "board/board_spi.h"
#include "stm32h7xx_ll_gpio.h"

spi_status_t ism330is_read_id(uint8_t *id)
{
    if (id == NULL) {
        return SPI_STATUS_INVALID_ARGUMENT;
    }

    /* Bit 7 selects read; the second byte clocks out WHO_AM_I. */
    const uint8_t tx[2] = { 0x80U | ISM330IS_WHO_AM_I_REG, 0x00U };
    uint8_t rx[2] = { 0U, 0U };

    LL_GPIO_ResetOutputPin(IMU1_CS_PORT, 1UL << IMU1_CS_PIN);
    spi_status_t status = spi_transfer(&imu1_spi_config, tx, rx, 2U, 20U);
    LL_GPIO_SetOutputPin(IMU1_CS_PORT, 1UL << IMU1_CS_PIN);

    if (status == SPI_STATUS_OK) {
        *id = rx[1];
    }
    return status;
}

