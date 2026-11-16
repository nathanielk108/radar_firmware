#include "drivers/bmi323.h"
#include "board/board_pins.h"
#include "board/board_spi.h"
#include "platform/time.h"
#include "stm32h7xx_ll_gpio.h"

#define BMI323_ACC_CONF 0x20U
#define BMI323_GYR_CONF 0x21U
#define BMI323_NORMAL_MODE (4U << 12)
#define BMI323_RANGE_250_DPS (1U << 4)
#define BMI323_ODR_800_HZ 0x0BU

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

static bmi323_status_t transfer(const uint8_t *tx, uint8_t *rx,
                                uint16_t count) {
  LL_GPIO_ResetOutputPin(IMU2_CS_PORT, 1UL << IMU2_CS_PIN);
  spi_status_t status = spi_transfer(&imu2_spi_config, tx, rx, count, 20U);
  LL_GPIO_SetOutputPin(IMU2_CS_PORT, 1UL << IMU2_CS_PIN);
  time_delay_us(2U);
  return map_spi_status(status);
}

bmi323_status_t bmi323_read_id(uint16_t *id) {
  if (!id)
    return BMI323_ERROR_INVALID_ARGUMENT;
  /* Address, dummy byte, little-endian 16-bit register value. */
  const uint8_t tx[] = {0x80U | BMI323_CHIP_ID_REG, 0U, 0U, 0U};
  uint8_t rx[sizeof(tx)];
  bmi323_status_t status = transfer(tx, rx, sizeof(tx));
  if (status == BMI323_OK)
    *id = (uint16_t)rx[2] | ((uint16_t)rx[3] << 8);
  return status;
}

static bmi323_status_t write_reg(uint8_t reg, uint16_t value) {
  const uint8_t tx[] = {reg, (uint8_t)value, (uint8_t)(value >> 8)};
  return transfer(tx, NULL, sizeof(tx));
}

bmi323_status_t bmi323_init(void) {
  /* The first CS rising edge selects SPI. Its read data are invalid. */
  uint16_t id;
  bmi323_status_t status = bmi323_read_id(&id);
  if (status != BMI323_OK)
    return status;
  time_delay_us(200U);

  status = bmi323_read_id(&id);
  if (status != BMI323_OK)
    return status;
  if ((id & 0xFFU) != BMI323_CHIP_ID_VAL)
    return BMI323_ERROR_DEVICE_ID;

  /* Use ±2 g and ±250 dps. Both sensors run in continuous normal mode. */
  status = write_reg(BMI323_ACC_CONF, BMI323_NORMAL_MODE | BMI323_ODR_800_HZ);
  if (status != BMI323_OK)
    return status;
  return write_reg(BMI323_GYR_CONF, BMI323_NORMAL_MODE | BMI323_RANGE_250_DPS |
                                        BMI323_ODR_800_HZ);
}
