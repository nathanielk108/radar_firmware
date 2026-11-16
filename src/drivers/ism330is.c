#include "drivers/ism330is.h"
#include "board/board_pins.h"
#include "board/board_spi.h"
#include "stm32h7xx_ll_gpio.h"

#define ISM330IS_CTRL1_XL 0x10U
#define ISM330IS_CTRL3_C 0x12U
#define ISM330IS_ODR_833_HZ (7U << 4)
#define ISM330IS_BDU (1U << 6)
#define ISM330IS_IF_INC (1U << 2)

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

static ism330is_status_t transfer(const uint8_t *tx, uint8_t *rx,
                                  uint16_t count) {
  LL_GPIO_ResetOutputPin(IMU1_CS_PORT, 1UL << IMU1_CS_PIN);
  spi_status_t status = spi_transfer(&imu1_spi_config, tx, rx, count, 20U);
  LL_GPIO_SetOutputPin(IMU1_CS_PORT, 1UL << IMU1_CS_PIN);
  return map_spi_status(status);
}

ism330is_status_t ism330is_read_id(uint8_t *id) {
  if (!id)
    return ISM330IS_ERROR_INVALID_ARGUMENT;
  const uint8_t tx[] = {0x80U | ISM330IS_WHO_AM_I_REG, 0U};
  uint8_t rx[sizeof(tx)];
  ism330is_status_t status = transfer(tx, rx, sizeof(tx));
  if (status == ISM330IS_OK)
    *id = rx[1];
  return status;
}

ism330is_status_t ism330is_init(void) {
  uint8_t id;
  ism330is_status_t status = ism330is_read_id(&id);
  if (status != ISM330IS_OK)
    return status;
  if (id != ISM330IS_WHO_AM_I_VAL)
    return ISM330IS_ERROR_DEVICE_ID;

  const uint8_t ctrl3[] = {ISM330IS_CTRL3_C, ISM330IS_BDU | ISM330IS_IF_INC};
  status = transfer(ctrl3, NULL, sizeof(ctrl3));
  if (status != ISM330IS_OK)
    return status;

  /* CTRL1_XL and CTRL2_G are adjacent. Use ±2 g and ±250 dps. */
  const uint8_t sensors[] = {ISM330IS_CTRL1_XL, ISM330IS_ODR_833_HZ,
                             ISM330IS_ODR_833_HZ};
  return transfer(sensors, NULL, sizeof(sensors));
}
