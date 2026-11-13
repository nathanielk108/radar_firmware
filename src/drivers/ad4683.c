#include "drivers/ad4683.h"
#include "board/board_spi.h"
#include "board/board_pins.h"

spi_status_t ad4683_transfer(uint16_t command, uint16_t *a, uint16_t *b)
{
    if (!a || !b) return SPI_STATUS_INVALID_ARGUMENT;
    return spi_transfer_pair(
        &adc_master_spi_config, &adc_slave_spi_config,
        command, a, b, ADC_A_CS_PORT, ADC_A_CS_PIN, 10U);
}

spi_status_t ad4683_read_sample(ad4683_sample_t *sample)
{
    if (!sample) {
        return SPI_STATUS_INVALID_ARGUMENT;
    }

    uint16_t a = 0;
    uint16_t b = 0;

    spi_status_t status = ad4683_transfer(0x0000U, &a, &b);

    if (status != SPI_STATUS_OK) {
        return status;
    }

    sample->a = (int16_t)a;
    sample->b = (int16_t)b;

    return SPI_STATUS_OK;
}


spi_status_t ad4683_init(void)
{
    /* CONFIGURATION1 REFSEL: use external 3.3 V on REFIO.
       Assumes reset-default configuration, as in the current bring-up.
       The first conversion returned after this write is discarded. */
    uint16_t a, b;
    return ad4683_transfer(0x9002U, &a, &b);
}
