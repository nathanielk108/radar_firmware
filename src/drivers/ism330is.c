#include "drivers/ism330is.h"
#include <stdio.h>

#include "board/board_pins.h"
#include "board/board_spi.h"
#include "stm32h7xx_ll_gpio.h"

static ism330is_status_t map_spi_status(spi_status_t status) {
  switch (status) {
  case SPI_STATUS_OK:
    return ISM330IS_OK;
  case SPI_STATUS_INVALID_ARGUMENT:
    return ISM330IS_ERROR_INVALID_ARGUMENT;
  case SPI_STATUS_TIMEOUT:
    return ISM330IS_ERROR_TIMEOUT;
  case SPI_STATUS_OVERRUN:
    return ISM330IS_ERROR_OVERRUN;
  case SPI_STATUS_MODE_FAULT:
    return ISM330IS_ERROR_MODE_FAULT;
  default:
    return ISM330IS_ERROR_SPI;
  }
}

const char *ism330is_status_name(ism330is_status_t status) {
  switch (status) {
  case ISM330IS_OK:
    return "OK";
  case ISM330IS_ERROR_INVALID_ARGUMENT:
    return "invalid argument";
  case ISM330IS_ERROR_TIMEOUT:
    return "SPI timeout";
  case ISM330IS_ERROR_OVERRUN:
    return "SPI overrun";
  case ISM330IS_ERROR_MODE_FAULT:
    return "SPI mode fault";
  case ISM330IS_ERROR_SPI:
    return "SPI error";
  case ISM330IS_ERROR_DEVICE_ID:
    return "device ID mismatch";
  default:
    return "unknown";
  }
}

ism330is_status_t ism330is_read_id(uint8_t *id) {
  if (id == NULL) {
    return ISM330IS_ERROR_INVALID_ARGUMENT;
  }

  /* Bit 7 selects read; the second byte clocks out WHO_AM_I. */
  const uint8_t tx[2] = {0x80U | ISM330IS_WHO_AM_I_REG, 0x00U};
  uint8_t rx[2] = {0U, 0U};

  LL_GPIO_ResetOutputPin(IMU1_CS_PORT, 1UL << IMU1_CS_PIN);
  spi_status_t transfer = spi_transfer(&imu1_spi_config, tx, rx, 2U, 20U);
  LL_GPIO_SetOutputPin(IMU1_CS_PORT, 1UL << IMU1_CS_PIN);

  if (transfer == SPI_STATUS_OK) {
    *id = rx[1];
  }
  return map_spi_status(transfer);
}

ism330is_status_t ism330is_init(void) {
  uint8_t id = 0U;
  ism330is_status_t status = ism330is_read_id(&id);
  return status != ISM330IS_OK
             ? status
             : (id == ISM330IS_WHO_AM_I_VAL ? ISM330IS_OK
                                            : ISM330IS_ERROR_DEVICE_ID);
}

const char *ism330is_diagnostic(char *buf, size_t size) {
  if (!buf || !size)
    return "ISM330IS: invalid buffer";
  uint8_t id = 0U;
  ism330is_status_t status = ism330is_read_id(&id);
  if (status == ISM330IS_OK && id != ISM330IS_WHO_AM_I_VAL)
    status = ISM330IS_ERROR_DEVICE_ID;
  (void)snprintf(
      buf, size, "ISM330IS: WHO_AM_I=0x%02X %s (%s)", id,
      (status == ISM330IS_OK && id == ISM330IS_WHO_AM_I_VAL) ? "OK" : "ERROR",
      ism330is_status_name(status));
  return buf;
}
