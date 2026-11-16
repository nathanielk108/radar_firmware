#include "services/motion.h"

#include "board/board_spi.h"
#include "drivers/bmi323.h"
#include "drivers/ism330is.h"
#include "platform/time.h"
#include "system/log.h"

static bool spi_ready[2];
static bool sensor_ready[2];

static bool valid_sensor(motion_sensor_t sensor) {
  return sensor == MOTION_ISM330IS || sensor == MOTION_BMI323;
}

void motion_init(void) {
  board_spi_imu1_init();
  board_spi_imu2_init();
  spi_ready[MOTION_ISM330IS] = spi_init(&imu1_spi_config) == SPI_STATUS_OK;
  spi_ready[MOTION_BMI323] = spi_init(&imu2_spi_config) == SPI_STATUS_OK;

  ism330is_status_t ism_status = spi_ready[MOTION_ISM330IS]
                                     ? ism330is_init()
                                     : ISM330IS_ERROR_SPI;
  sensor_ready[MOTION_ISM330IS] = ism_status == ISM330IS_OK;
  if (sensor_ready[MOTION_ISM330IS])
    LOG_INFO("ISM330IS: ready");
  else
    LOG_ERROR("ISM330IS: %s", ism330is_status_name(ism_status));

  bmi323_status_t bmi_status =
      spi_ready[MOTION_BMI323] ? bmi323_init() : BMI323_ERROR_SPI;
  sensor_ready[MOTION_BMI323] = bmi_status == BMI323_OK;
  if (sensor_ready[MOTION_BMI323])
    LOG_INFO("BMI323: ready");
  else
    LOG_ERROR("BMI323: %s", bmi323_status_name(bmi_status));
}

bool motion_ready(motion_sensor_t sensor) {
  return valid_sensor(sensor) && sensor_ready[sensor];
}

const char *motion_status_name(motion_status_t status) {
  switch (status) {
  case MOTION_OK:
    return "OK";
  case MOTION_NO_NEW_SAMPLE:
    return "no new sample";
  case MOTION_UNAVAILABLE:
    return "unavailable";
  case MOTION_IO_ERROR:
    return "SPI error";
  case MOTION_INVALID_ARGUMENT:
    return "invalid argument";
  default:
    return "unknown";
  }
}

motion_status_t motion_read_id(motion_sensor_t sensor, uint16_t *id) {
  if (!id || !valid_sensor(sensor))
    return MOTION_INVALID_ARGUMENT;
  if (!spi_ready[sensor])
    return MOTION_UNAVAILABLE;
  if (sensor == MOTION_BMI323)
    return bmi323_read_id(id) == BMI323_OK ? MOTION_OK : MOTION_IO_ERROR;
  uint8_t value;
  if (ism330is_read_id(&value) != ISM330IS_OK)
    return MOTION_IO_ERROR;
  *id = value;
  return MOTION_OK;
}

motion_status_t motion_read(motion_sensor_t sensor, motion_sample_t *sample) {
  if (!sample || !valid_sensor(sensor))
    return MOTION_INVALID_ARGUMENT;
  if (!motion_ready(sensor))
    return MOTION_UNAVAILABLE;

  int16_t accel[3], gyro[3];
  uint32_t timestamp = time_ms();
  if (sensor == MOTION_ISM330IS) {
    ism330is_status_t status = ism330is_read_sample(accel, gyro);
    if (status != ISM330IS_OK)
      return status == ISM330IS_NO_NEW_SAMPLE ? MOTION_NO_NEW_SAMPLE
                                             : MOTION_IO_ERROR;
    /* 61 µg/LSB at ±2 g and 8.75 mdps/LSB at ±250 dps. */
    for (unsigned i = 0; i < 3U; ++i) {
      sample->accel_ug[i] = accel[i] * 61;
      sample->gyro_mdps[i] = gyro[i] * 35 / 4;
    }
  } else {
    bmi323_status_t status = bmi323_read_sample(accel, gyro);
    if (status != BMI323_OK)
      return status == BMI323_NO_NEW_SAMPLE ? MOTION_NO_NEW_SAMPLE
                                          : MOTION_IO_ERROR;
    /* ±2 g and ±250 dps scaled by the signed 16-bit full scale. */
    for (unsigned i = 0; i < 3U; ++i) {
      sample->accel_ug[i] = accel[i] * 15625 / 256;
      sample->gyro_mdps[i] = gyro[i] * 31250 / 4096;
    }
  }
  sample->timestamp_ms = timestamp;
  return MOTION_OK;
}
