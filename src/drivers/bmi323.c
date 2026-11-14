#include "drivers/bmi323.h"
#include <stdio.h>

#include "board/board_pins.h"
#include "board/board_spi.h"
#include "stm32h7xx_ll_gpio.h"

static bmi323_status_t map_spi_status(spi_status_t status) {
  switch (status) {
  case SPI_STATUS_OK:
    return BMI323_OK;
  case SPI_STATUS_INVALID_ARGUMENT:
    return BMI323_ERROR_INVALID_ARGUMENT;
  case SPI_STATUS_TIMEOUT:
    return BMI323_ERROR_TIMEOUT;
  case SPI_STATUS_OVERRUN:
    return BMI323_ERROR_OVERRUN;
  case SPI_STATUS_MODE_FAULT:
    return BMI323_ERROR_MODE_FAULT;
  default:
    return BMI323_ERROR_SPI;
  }
}

const char *bmi323_status_name(bmi323_status_t status) {
  switch (status) {
  case BMI323_OK:
    return "OK";
  case BMI323_ERROR_INVALID_ARGUMENT:
    return "invalid argument";
  case BMI323_ERROR_TIMEOUT:
    return "SPI timeout";
  case BMI323_ERROR_OVERRUN:
    return "SPI overrun";
  case BMI323_ERROR_MODE_FAULT:
    return "SPI mode fault";
  case BMI323_ERROR_SPI:
    return "SPI error";
  case BMI323_ERROR_DEVICE_ID:
    return "device ID mismatch";
  default:
    return "unknown";
  }
}

static bool spi_selected;

static bmi323_status_t bmi323_read_reg(uint8_t reg, uint16_t *value) {
  if (value == NULL) {
    return BMI323_ERROR_INVALID_ARGUMENT;
  }

  /* Address, dummy byte, little-endian 16-bit register value. */
  const uint8_t tx[4] = {(uint8_t)(0x80U | reg), 0U, 0U, 0U};
  uint8_t rx[4] = {0U, 0U, 0U, 0U};

  LL_GPIO_ResetOutputPin(IMU2_CS_PORT, 1UL << IMU2_CS_PIN);
  spi_status_t transfer = spi_transfer(&imu2_spi_config, tx, rx, 4U, 20U);
  LL_GPIO_SetOutputPin(IMU2_CS_PORT, 1UL << IMU2_CS_PIN);

  if (transfer == SPI_STATUS_OK) {
    *value = (uint16_t)rx[2] | ((uint16_t)rx[3] << 8);
  }
  return map_spi_status(transfer);
}

bmi323_status_t bmi323_read_id(uint16_t *id) {
  if (id == NULL) {
    return BMI323_ERROR_INVALID_ARGUMENT;
  }

  /* The first SPI access after power-on selects SPI mode. */
  if (!spi_selected) {
    uint16_t discard;
    bmi323_status_t status = bmi323_read_reg(BMI323_CHIP_ID_REG, &discard);
    if (status != BMI323_OK)
      return status;
    spi_selected = true;
  }
  return bmi323_read_reg(BMI323_CHIP_ID_REG, id);
}

bmi323_status_t bmi323_init(void) {
  spi_selected = false;
  uint16_t id = 0U;
  bmi323_status_t status = bmi323_read_id(&id);
  return status != BMI323_OK
             ? status
             : ((id & 0xFFU) == BMI323_CHIP_ID_VAL ? BMI323_OK
                                                   : BMI323_ERROR_DEVICE_ID);
}

const char *bmi323_diagnostic(char *buf, size_t size) {
  if (!buf || !size)
    return "BMI323: invalid buffer";
  uint16_t id = 0U;
  bmi323_status_t status = bmi323_read_id(&id);
  if (status == BMI323_OK && (id & 0xFFU) != BMI323_CHIP_ID_VAL)
    status = BMI323_ERROR_DEVICE_ID;
  (void)snprintf(buf, size, "BMI323: CHIP_ID=0x%02X %s (%s)", (id & 0xFFU),
                 (status == BMI323_OK && (id & 0xFFU) == BMI323_CHIP_ID_VAL)
                     ? "OK"
                     : "ERROR",
                 bmi323_status_name(status));
  return buf;
}
