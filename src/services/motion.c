#include "services/motion.h"

#include "board/board_gpio.h"
#include "board/board_pins.h"
#include "board/board_spi.h"
#include "drivers/bmi323.h"
#include "drivers/ism330is.h"
#include "platform/time.h"
#include "stm32h7xx_ll_exti.h"
#include "stm32h7xx_ll_gpio.h"
#include "stm32h7xx_ll_system.h"
#include "system/log.h"
#include <stddef.h>

#define MOTION_SENSOR_COUNT 2U
#define MOTION_QUEUE_SLOTS (MOTION_QUEUE_CAPACITY + 1U)
#define MOTION_RETRY_US 100U
#define MOTION_IRQ_PRIORITY 8U

typedef struct {
  uint32_t timestamp_us;
  uint32_t sequence;
} motion_edge_t;

typedef struct {
  motion_raw_sample_t samples[MOTION_QUEUE_SLOTS];
  volatile motion_edge_t edge;
  uint32_t handled_sequence;
  uint32_t last_attempt_us;
  uint32_t missed_events;
  uint32_t queue_overruns;
  uint32_t io_errors;
  uint8_t head;
  uint8_t tail;
  bool has_latest;
  bool resync;
  bool spi_ready;
  bool sensor_ready;
} motion_stream_t;

static motion_stream_t streams[MOTION_SENSOR_COUNT];
static const motion_profile_t profiles[MOTION_SENSOR_COUNT] = {
    [MOTION_ISM330IS] = {.odr_hz = 833U,
                        .accel_ug_per_lsb = 61.0f,
                        .gyro_mdps_per_lsb = 8.75f},
    [MOTION_BMI323] = {.odr_hz = 800U,
                      .accel_ug_per_lsb = 2000000.0f / 32768.0f,
                      .gyro_mdps_per_lsb = 250000.0f / 32768.0f},
};

static bool valid_sensor(motion_sensor_t sensor) {
  return (unsigned)sensor < MOTION_SENSOR_COUNT;
}

static void drdy_init(GPIO_TypeDef *port, uint8_t pin, uint32_t source,
                      uint32_t route, IRQn_Type irq) {
  const uint32_t line = 1UL << pin;
  board_gpio_pin(port, pin, LL_GPIO_MODE_INPUT, LL_GPIO_OUTPUT_PUSHPULL,
                 LL_GPIO_PULL_NO, LL_GPIO_SPEED_FREQ_LOW, 0U, false);
  LL_SYSCFG_SetEXTISource(source, route);
  LL_EXTI_DisableFallingTrig_0_31(line);
  LL_EXTI_EnableRisingTrig_0_31(line);
  LL_EXTI_ClearFlag_0_31(line);
  NVIC_ClearPendingIRQ(irq);
  NVIC_SetPriority(irq, MOTION_IRQ_PRIORITY);
  LL_EXTI_EnableIT_0_31(line);
  NVIC_EnableIRQ(irq);
}

static void drdy_irq(motion_sensor_t sensor, uint32_t line) {
  if (!LL_EXTI_IsActiveFlag_0_31(line))
    return;
  LL_EXTI_ClearFlag_0_31(line);
  streams[sensor].edge.timestamp_us = time_us();
  ++streams[sensor].edge.sequence;
}

void EXTI9_5_IRQHandler(void) {
  drdy_irq(MOTION_ISM330IS, 1UL << IMU1_DRDY_PIN);
}

void EXTI4_IRQHandler(void) {
  drdy_irq(MOTION_BMI323, 1UL << IMU2_DRDY_PIN);
}

static motion_edge_t edge_snapshot(const motion_stream_t *stream) {
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  motion_edge_t edge = stream->edge;
  __set_PRIMASK(primask);
  return edge;
}

void motion_init(void) {
  board_spi_imu1_init();
  board_spi_imu2_init();
  motion_stream_t *ism = &streams[MOTION_ISM330IS];
  motion_stream_t *bmi = &streams[MOTION_BMI323];
  ism->spi_ready = spi_init(&imu1_spi_config) == SPI_STATUS_OK;
  bmi->spi_ready = spi_init(&imu2_spi_config) == SPI_STATUS_OK;

  ism330is_status_t ism_status =
      ism->spi_ready ? ism330is_init() : ISM330IS_ERROR_SPI;
  ism->sensor_ready = ism_status == ISM330IS_OK;
  if (ism->sensor_ready) {
    drdy_init(IMU1_DRDY_PORT, IMU1_DRDY_PIN, LL_SYSCFG_EXTI_PORTC,
              LL_SYSCFG_EXTI_LINE5, EXTI9_5_IRQn);
    LOG_INFO("ISM330IS: ready");
  } else {
    LOG_ERROR("ISM330IS: %s", ism330is_status_name(ism_status));
  }

  bmi323_status_t bmi_status =
      bmi->spi_ready ? bmi323_init() : BMI323_ERROR_SPI;
  bmi->sensor_ready = bmi_status == BMI323_OK;
  if (bmi->sensor_ready) {
    drdy_init(IMU2_DRDY_PORT, IMU2_DRDY_PIN, LL_SYSCFG_EXTI_PORTG,
              LL_SYSCFG_EXTI_LINE4, EXTI4_IRQn);
    LOG_INFO("BMI323: ready");
  } else {
    LOG_ERROR("BMI323: %s", bmi323_status_name(bmi_status));
  }
}

bool motion_ready(motion_sensor_t sensor) {
  return valid_sensor(sensor) && streams[sensor].sensor_ready;
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
  if (!streams[sensor].spi_ready)
    return MOTION_UNAVAILABLE;
  if (sensor == MOTION_BMI323)
    return bmi323_read_id(id) == BMI323_OK ? MOTION_OK : MOTION_IO_ERROR;
  uint8_t value;
  if (ism330is_read_id(&value) != ISM330IS_OK)
    return MOTION_IO_ERROR;
  *id = value;
  return MOTION_OK;
}

static motion_status_t read_raw(motion_sensor_t sensor,
                                motion_raw_sample_t *sample) {
  if (sensor == MOTION_ISM330IS) {
    const ism330is_status_t status =
        ism330is_read_sample(sample->accel, sample->gyro);
    if (status == ISM330IS_OK)
      return MOTION_OK;
    return status == ISM330IS_NO_NEW_SAMPLE ? MOTION_NO_NEW_SAMPLE
                                            : MOTION_IO_ERROR;
  }
  const bmi323_status_t status = bmi323_read_sample(sample->accel, sample->gyro);
  if (status == BMI323_OK)
    return MOTION_OK;
  return status == BMI323_NO_NEW_SAMPLE ? MOTION_NO_NEW_SAMPLE : MOTION_IO_ERROR;
}

static uint8_t next_slot(uint8_t slot) {
  return (uint8_t)((slot + 1U) % MOTION_QUEUE_SLOTS);
}

static void poll_sensor(motion_sensor_t sensor) {
  motion_stream_t *stream = &streams[sensor];
  if (!stream->sensor_ready || stream->edge.sequence == stream->handled_sequence)
    return;

  const uint32_t now = time_us();
  if (now - stream->last_attempt_us < MOTION_RETRY_US)
    return;
  stream->last_attempt_us = now;
  const motion_edge_t edge = edge_snapshot(stream);
  const uint32_t events = edge.sequence - stream->handled_sequence;

  /* The spare slot lets a driver populate the sample in place before commit. */
  motion_raw_sample_t *sample = &stream->samples[stream->head];
  const motion_status_t status = read_raw(sensor, sample);
  if (status == MOTION_NO_NEW_SAMPLE)
    return;

  stream->handled_sequence = edge.sequence;
  if (status != MOTION_OK) {
    ++stream->io_errors;
    stream->missed_events += events;
    stream->resync = true;
    return;
  }

  /* Drain a stale pair after missed edges or I/O faults. An edge during the
   * transaction makes the association between timestamp and data ambiguous. */
  if (events != 1U || stream->resync || stream->edge.sequence != edge.sequence) {
    stream->missed_events += events;
    stream->resync = false;
    return;
  }

  sample->timestamp_us = edge.timestamp_us;
  sample->sequence = edge.sequence;
  stream->head = next_slot(stream->head);
  if (stream->head == stream->tail) {
    stream->tail = next_slot(stream->tail);
    ++stream->queue_overruns;
  }
  stream->has_latest = true;
}

void motion_poll(void) {
  poll_sensor(MOTION_ISM330IS);
  poll_sensor(MOTION_BMI323);
}

const motion_raw_sample_t *motion_peek(motion_sensor_t sensor) {
  if (!motion_ready(sensor))
    return NULL;
  const motion_stream_t *stream = &streams[sensor];
  return stream->head != stream->tail ? &stream->samples[stream->tail] : NULL;
}

void motion_pop(motion_sensor_t sensor) {
  if (motion_peek(sensor))
    streams[sensor].tail = next_slot(streams[sensor].tail);
}

const motion_profile_t *motion_profile(motion_sensor_t sensor) {
  return valid_sensor(sensor) ? &profiles[sensor] : NULL;
}

bool motion_get_stats(motion_sensor_t sensor, motion_stats_t *stats) {
  if (!stats || !valid_sensor(sensor))
    return false;
  const motion_stream_t *stream = &streams[sensor];
  *stats = (motion_stats_t){
      .drdy_events = stream->edge.sequence,
      .missed_events = stream->missed_events,
      .queue_overruns = stream->queue_overruns,
      .io_errors = stream->io_errors,
      .queued = (MOTION_QUEUE_SLOTS + stream->head - stream->tail) %
                MOTION_QUEUE_SLOTS,
  };
  return true;
}

motion_status_t motion_read(motion_sensor_t sensor, motion_sample_t *sample) {
  if (!sample || !valid_sensor(sensor))
    return MOTION_INVALID_ARGUMENT;
  if (!motion_ready(sensor))
    return MOTION_UNAVAILABLE;
  const motion_stream_t *stream = &streams[sensor];
  if (!stream->has_latest)
    return MOTION_NO_NEW_SAMPLE;

  const uint8_t index = stream->head ? stream->head - 1U : MOTION_QUEUE_SLOTS - 1U;
  const motion_raw_sample_t *raw = &stream->samples[index];
  const motion_profile_t *profile = motion_profile(sensor);
  sample->timestamp_us = raw->timestamp_us;
  sample->timestamp_ms = raw->timestamp_us / 1000U;
  sample->sequence = raw->sequence;
  for (unsigned i = 0; i < 3U; ++i) {
    sample->accel_ug[i] = (int32_t)(raw->accel[i] * profile->accel_ug_per_lsb);
    sample->gyro_mdps[i] = (int32_t)(raw->gyro[i] * profile->gyro_mdps_per_lsb);
  }
  return MOTION_OK;
}
