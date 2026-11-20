#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum { MOTION_ISM330IS, MOTION_BMI323 } motion_sensor_t;

typedef enum {
  MOTION_OK,
  MOTION_NO_NEW_SAMPLE,
  MOTION_UNAVAILABLE,
  MOTION_IO_ERROR,
  MOTION_INVALID_ARGUMENT
} motion_status_t;

typedef struct {
  /* Milliseconds derived from timestamp_us, with the same rollover. */
  uint32_t timestamp_ms;
  int32_t accel_ug[3];
  int32_t gyro_mdps[3];
  uint32_t timestamp_us;
  uint32_t sequence;
} motion_sample_t;

typedef struct {
  /* Host time_us() at the gyro data-ready interrupt. Accel and gyro use
   * sensor-native X/Y/Z and both must have updated for publication. */
  uint32_t timestamp_us;
  uint32_t sequence; /* Per-IMU data-ready edge count, wraps naturally. */
  int16_t accel[3];
  int16_t gyro[3];
} motion_raw_sample_t;

typedef struct {
  uint16_t odr_hz;
  float accel_ug_per_lsb;
  float gyro_mdps_per_lsb;
} motion_profile_t;

typedef struct {
  uint32_t drdy_events;
  uint32_t missed_events;   /* Observed edges discarded before publication. */
  uint32_t queue_overruns;  /* Published samples evicted before consumption. */
  uint32_t io_errors;
  uint32_t queued;
} motion_stats_t;

#define MOTION_QUEUE_CAPACITY 64U

void motion_init(void);
bool motion_ready(motion_sensor_t sensor);
const char *motion_status_name(motion_status_t status);

motion_status_t motion_read_id(motion_sensor_t sensor, uint16_t *id);

/* Foreground only. Call regularly to drain ready output registers. Delays
 * that span multiple data-ready edges can discard unread hardware samples.
 * Each IMU has one queue consumer. Peek borrows its oldest sample until the
 * next poll or pop. A full queue drops its oldest sample and keeps the newest.
 */
void motion_poll(void);
const motion_raw_sample_t *motion_peek(motion_sensor_t sensor);
void motion_pop(motion_sensor_t sensor);
const motion_profile_t *motion_profile(motion_sensor_t sensor);
bool motion_get_stats(motion_sensor_t sensor, motion_stats_t *stats);

/* Foreground diagnostic snapshot of the latest published sample. Does not
 * consume the queue. Timestamp is shared with radar through time_us(). */
motion_status_t motion_read(motion_sensor_t sensor, motion_sample_t *sample);
