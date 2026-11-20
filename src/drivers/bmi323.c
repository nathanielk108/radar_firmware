#include "drivers/bmi323.h"
#include "board/board_pins.h"
#include "board/board_spi.h"
#include "platform/time.h"
#include "stm32h7xx_ll_gpio.h"

#define BMI323_ACC_CONF 0x20U
#define BMI323_GYR_CONF 0x21U
#define BMI323_STATUS 0x02U
#define BMI323_ACC_DATA_X 0x03U
#define BMI323_IO_INT_CTRL 0x38U
#define BMI323_INT_CONF 0x39U
#define BMI323_INT_MAP2 0x3BU
#define BMI323_DATA_READY 0xC0U /* drdy_acc and drdy_gyr */
#define BMI323_NORMAL_MODE (4U << 12)
#define BMI323_RANGE_250_DPS (1U << 4)
#define BMI323_ODR_800_HZ 0x0BU
#define BMI323_INT1_ACTIVE_HIGH (1U << 0)
#define BMI323_INT1_OUTPUT_ENABLE (1U << 2)
#define BMI323_GYR_DRDY_ON_INT1 (1U << 8)

static uint16_t pending_data;

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
  case BMI323_NO_NEW_SAMPLE:
    return "no new sample";
  default:
    return "unknown";
  }
}

static bmi323_status_t transfer(const uint8_t *tx, uint8_t *rx,
                                uint16_t count) {
  LL_GPIO_ResetOutputPin(IMU2_CS_PORT, 1UL << IMU2_CS_PIN);
  spi_status_t status = spi_transfer(&imu2_spi_config, tx, rx, count, 2U);
  LL_GPIO_SetOutputPin(IMU2_CS_PORT, 1UL << IMU2_CS_PIN);
  time_delay_us(2U);
  return map_spi_status(status);
}

static uint16_t read_word(const uint8_t *bytes) {
  return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static bmi323_status_t read_reg(uint8_t reg, uint16_t *value) {
  /* Address, dummy byte, little-endian 16-bit register value. */
  const uint8_t tx[] = {0x80U | reg, 0U, 0U, 0U};
  uint8_t rx[sizeof(tx)];
  bmi323_status_t status = transfer(tx, rx, sizeof(tx));
  if (status == BMI323_OK)
    *value = read_word(&rx[2]);
  return status;
}

bmi323_status_t bmi323_read_id(uint16_t *id) {
  if (!id)
    return BMI323_ERROR_INVALID_ARGUMENT;
  return read_reg(BMI323_CHIP_ID_REG, id);
}

static bmi323_status_t write_reg(uint8_t reg, uint16_t value) {
  const uint8_t tx[] = {reg, (uint8_t)value, (uint8_t)(value >> 8)};
  return transfer(tx, NULL, sizeof(tx));
}

bmi323_status_t bmi323_init(void) {
  pending_data = 0U;
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

  /* Active-high push-pull gyro data-ready pulses on board-wired INT1. */
  status = write_reg(BMI323_IO_INT_CTRL, BMI323_INT1_ACTIVE_HIGH |
                                            BMI323_INT1_OUTPUT_ENABLE);
  if (status != BMI323_OK)
    return status;
  status = write_reg(BMI323_INT_CONF, 0U);
  if (status != BMI323_OK)
    return status;
  status = write_reg(BMI323_INT_MAP2, BMI323_GYR_DRDY_ON_INT1);
  if (status != BMI323_OK)
    return status;

  /* Use ±2 g and ±250 dps. Both sensors run in continuous normal mode. */
  status = write_reg(BMI323_ACC_CONF, BMI323_NORMAL_MODE | BMI323_ODR_800_HZ);
  if (status != BMI323_OK)
    return status;
  return write_reg(BMI323_GYR_CONF, BMI323_NORMAL_MODE | BMI323_RANGE_250_DPS |
                                        BMI323_ODR_800_HZ);
}

bmi323_status_t bmi323_read_sample(int16_t accel[3], int16_t gyro[3]) {
  if (!accel || !gyro)
    return BMI323_ERROR_INVALID_ARGUMENT;
  uint16_t ready;
  bmi323_status_t status = read_reg(BMI323_STATUS, &ready);
  if (status != BMI323_OK)
    return status;
  /* STATUS is clear-on-read. Keep each bit until both sensors have updated. */
  pending_data |= ready & BMI323_DATA_READY;
  if (pending_data != BMI323_DATA_READY)
    return BMI323_NO_NEW_SAMPLE;

  /* Address and dummy byte followed by six auto-incremented 16-bit words. */
  const uint8_t tx[14] = {0x80U | BMI323_ACC_DATA_X};
  uint8_t rx[sizeof(tx)];
  status = transfer(tx, rx, sizeof(tx));
  if (status != BMI323_OK)
    return status;
  pending_data = 0U;
  for (uint8_t i = 0; i < 3U; ++i) {
    unsigned byte = 2U + 2U * i;
    accel[i] = (int16_t)read_word(&rx[byte]);
    gyro[i] = (int16_t)read_word(&rx[byte + 6U]);
  }
  return BMI323_OK;
}
