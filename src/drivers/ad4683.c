#include "drivers/ad4683.h"
#include "board/board_spi.h"

spi_status_t ad4683_init(void)
{
    spi_status_t status;

    status = spi_init(&adc_slave_spi_config);
    if (status != SPI_STATUS_OK) {
        return status;
    }

    return spi_init(&adc_master_spi_config);
}

spi_status_t ad4683_set_external_reference(void) {
    uint16_t a, b;

    return spi_transfer_pair(
        &adc_master_spi_config,
        &adc_slave_spi_config,
        0x9002U,
        &a,
        &b,
        10U
    );
}

spi_status_t ad4683_read_sample(ad4683_sample_t *sample)
{
    if (!sample) {
        return SPI_STATUS_INVALID_ARGUMENT;
    }

    uint16_t a = 0;
    uint16_t b = 0;

    spi_status_t status = spi_transfer_pair(
        &adc_master_spi_config,
        &adc_slave_spi_config,
        0x0000U,
        &a,
        &b,
        10U
    );

    if (status != SPI_STATUS_OK) {
        return status;
    }

    sample->a = (int16_t)a;
    sample->b = (int16_t)b;

    return SPI_STATUS_OK;
}

