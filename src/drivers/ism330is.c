#include "drivers/ism330is.h"
#include "board/board_pins.h"
#include "board/board_spi.h"
#include "stm32h7xx_ll_gpio.h"

#define ISM330IS_DRDY_PULSED_REG 0x0BU
#define ISM330IS_INT2_CTRL 0x0EU
#define ISM330IS_CTRL1_XL 0x10U
#define ISM330IS_CTRL3_C 0x12U
#define ISM330IS_STATUS_REG 0x1EU
#define ISM330IS_OUTX_L_G 0x22U
#define ISM330IS_DATA_READY 0x03U /* GDA and XLDA */
#define ISM330IS_ODR_833_HZ (7U << 4)
#define ISM330IS_BDU (1U << 6)
#define ISM330IS_IF_INC (1U << 2)
#define ISM330IS_DRDY_PULSED (1U << 7)
#define ISM330IS_INT2_DRDY_G (1U << 1)

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
  case ISM330IS_NO_NEW_SAMPLE:
    return "no new sample";
  default:
    return "unknown";
  }
}

static ism330is_status_t transfer(const uint8_t *tx, uint8_t *rx,
                                  uint16_t count) {
  LL_GPIO_ResetOutputPin(IMU1_CS_PORT, 1UL << IMU1_CS_PIN);
  spi_status_t status = spi_transfer(&imu1_spi_config, tx, rx, count, 2U);
  LL_GPIO_SetOutputPin(IMU1_CS_PORT, 1UL << IMU1_CS_PIN);
  return map_spi_status(status);
}

static ism330is_status_t read_reg(uint8_t reg, uint8_t *value) {
  const uint8_t tx[] = {0x80U | reg, 0U};
  uint8_t rx[sizeof(tx)];
  ism330is_status_t status = transfer(tx, rx, sizeof(tx));
  if (status == ISM330IS_OK)
    *value = rx[1];
  return status;
}

ism330is_status_t ism330is_read_id(uint8_t *id) {
  if (!id)
    return ISM330IS_ERROR_INVALID_ARGUMENT;
  return read_reg(ISM330IS_WHO_AM_I_REG, id);
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
  const uint8_t drdy[] = {ISM330IS_DRDY_PULSED_REG, ISM330IS_DRDY_PULSED};
  status = transfer(drdy, NULL, sizeof(drdy));
  if (status != ISM330IS_OK)
    return status;
  const uint8_t int2[] = {ISM330IS_INT2_CTRL, ISM330IS_INT2_DRDY_G};
  status = transfer(int2, NULL, sizeof(int2));
  if (status != ISM330IS_OK)
    return status;

  /* CTRL1_XL and CTRL2_G are adjacent. Use ±2 g and ±250 dps. */
  const uint8_t sensors[] = {ISM330IS_CTRL1_XL, ISM330IS_ODR_833_HZ,
                             ISM330IS_ODR_833_HZ};
  return transfer(sensors, NULL, sizeof(sensors));
}

ism330is_status_t ism330is_read_sample(int16_t accel[3], int16_t gyro[3]) {
  if (!accel || !gyro)
    return ISM330IS_ERROR_INVALID_ARGUMENT;
  uint8_t ready;
  ism330is_status_t status = read_reg(ISM330IS_STATUS_REG, &ready);
  if (status != ISM330IS_OK)
    return status;
  if ((ready & ISM330IS_DATA_READY) != ISM330IS_DATA_READY)
    return ISM330IS_NO_NEW_SAMPLE;

  /* Auto-increment reads three gyro axes followed by three accel axes. */
  const uint8_t tx[13] = {0x80U | ISM330IS_OUTX_L_G};
  uint8_t rx[sizeof(tx)];
  status = transfer(tx, rx, sizeof(tx));
  if (status != ISM330IS_OK)
    return status;
  for (uint8_t i = 0; i < 3U; ++i) {
    unsigned byte = 1U + 2U * i;
    gyro[i] = (int16_t)((uint16_t)rx[byte] | ((uint16_t)rx[byte + 1U] << 8));
    accel[i] =
        (int16_t)((uint16_t)rx[byte + 6U] | ((uint16_t)rx[byte + 7U] << 8));
  }
  return ISM330IS_OK;
}
