#include "drivers/ad4683.h"
#include "board/board_pins.h"
#include "board/board_spi.h"
#include "platform/time.h"

#define AD4683_NOP 0x0000U
#define AD4683_CONFIG1 0x1000U
#define AD4683_CONFIG2 0x2000U
#define AD4683_WRITE 0x8000U
#define AD4683_REGISTER_DATA 0x0FFFU
#define AD4683_REFSEL_EXTERNAL 0x0002U
#define AD4683_TWO_WIRE 0x0000U

static ad4683_status_t map_spi_status(spi_status_t status) {
  switch (status) {
  case SPI_STATUS_OK:
    return AD4683_OK;
  case SPI_STATUS_INVALID_ARGUMENT:
    return AD4683_ERROR_INVALID_ARGUMENT;
  case SPI_STATUS_TIMEOUT:
    return AD4683_ERROR_TIMEOUT;
  case SPI_STATUS_OVERRUN:
    return AD4683_ERROR_OVERRUN;
  case SPI_STATUS_MODE_FAULT:
    return AD4683_ERROR_MODE_FAULT;
  default:
    return AD4683_ERROR_SPI;
  }
}

const char *ad4683_status_name(ad4683_status_t status) {
  switch (status) {
  case AD4683_OK:
    return "OK";
  case AD4683_ERROR_INVALID_ARGUMENT:
    return "invalid argument";
  case AD4683_ERROR_TIMEOUT:
    return "SPI timeout";
  case AD4683_ERROR_OVERRUN:
    return "SPI overrun";
  case AD4683_ERROR_MODE_FAULT:
    return "SPI mode fault";
  case AD4683_ERROR_SPI:
    return "SPI error";
  case AD4683_ERROR_CONFIG:
    return "configuration readback mismatch";
  default:
    return "unknown";
  }
}

ad4683_status_t ad4683_transfer(uint16_t command, uint16_t *a, uint16_t *b) {
  if (!a || !b)
    return AD4683_ERROR_INVALID_ARGUMENT;

  spi_status_t status =
      spi_transfer_pair(&adc_master_spi_config, &adc_slave_spi_config, command,
                        a, b, ADC_A_CS_PORT, ADC_A_CS_PIN, 10U);
  /* The AD4683 requires CS high for at least 1.5 us between transactions. */
  time_delay_us(2U);
  return map_spi_status(status);
}

ad4683_status_t ad4683_read_sample(ad4683_sample_t *sample) {
  if (!sample)
    return AD4683_ERROR_INVALID_ARGUMENT;

  uint16_t a, b;
  ad4683_status_t status = ad4683_transfer(AD4683_NOP, &a, &b);
  if (status == AD4683_OK) {
    sample->a = (int16_t)a;
    sample->b = (int16_t)b;
  }
  return status;
}

static ad4683_status_t configure_register(uint16_t address, uint16_t value) {
  uint16_t a, b;
  ad4683_status_t status =
      ad4683_transfer(AD4683_WRITE | address | value, &a, &b);
  if (status != AD4683_OK)
    return status;
  status = ad4683_transfer(address, &a, &b);
  if (status != AD4683_OK)
    return status;
  status = ad4683_transfer(AD4683_NOP, &a, &b);
  if (status == AD4683_OK && (a & AD4683_REGISTER_DATA) != value)
    return AD4683_ERROR_CONFIG;
  return status;
}

ad4683_status_t ad4683_init(void) {
  ad4683_status_t status =
      configure_register(AD4683_CONFIG1, AD4683_REFSEL_EXTERNAL);
  if (status != AD4683_OK)
    return status;
  return configure_register(AD4683_CONFIG2, AD4683_TWO_WIRE);
}
