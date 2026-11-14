#include "drivers/ad4683.h"
#include "board/board_pins.h"
#include "board/board_spi.h"
#include <stdio.h>

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
  default:
    return "unknown";
  }
}

ad4683_status_t ad4683_transfer(uint16_t command, uint16_t *a, uint16_t *b) {
  if (!a || !b)
    return AD4683_ERROR_INVALID_ARGUMENT;
  return map_spi_status(spi_transfer_pair(&adc_master_spi_config,
                                          &adc_slave_spi_config, command, a, b,
                                          ADC_A_CS_PORT, ADC_A_CS_PIN, 10U));
}

ad4683_status_t ad4683_read_sample(ad4683_sample_t *sample) {
  if (!sample) {
    return AD4683_ERROR_INVALID_ARGUMENT;
  }

  uint16_t a = 0;
  uint16_t b = 0;

  ad4683_status_t status = ad4683_transfer(0x0000U, &a, &b);

  if (status != AD4683_OK) {
    return status;
  }

  sample->a = (int16_t)a;
  sample->b = (int16_t)b;

  return AD4683_OK;
}

ad4683_status_t ad4683_init(void) {
  /* CONFIGURATION1 REFSEL: use external 3.3 V on REFIO.
     Assumes reset-default configuration, as in the current bring-up.
     The first conversion returned after this write is discarded. */
  uint16_t a, b;
  return ad4683_transfer(0x9002U, &a, &b);
}

const char *ad4683_diagnostic(char *buf, size_t size) {
  if (!buf || !size)
    return "AD4683: invalid buffer";
  ad4683_sample_t sample;
  ad4683_status_t status = ad4683_read_sample(&sample);
  if (status != AD4683_OK) {
    (void)snprintf(buf, size, "AD4683: %s", ad4683_status_name(status));
  } else {
    /* Codes are two's complement; external 3.3-V reference selected at init. */
    (void)snprintf(buf, size, "AD4683: A=%d B=%d LSB (3.3V ext, differential)",
                   (int)sample.a, (int)sample.b);
  }
  return buf;
}
