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
  /* Local time at the start of the poll. Axis order is sensor-native X/Y/Z. */
  uint32_t timestamp_ms;
  int32_t accel_ug[3];
  int32_t gyro_mdps[3];
} motion_sample_t;

void motion_init(void);
bool motion_ready(motion_sensor_t sensor);
const char *motion_status_name(motion_status_t status);

motion_status_t motion_read_id(motion_sensor_t sensor, uint16_t *id);

/* Foreground only. Reads the latest set after both data-ready flags have fired.
 * Returns immediately when data are pending. Timestamp does not imply hardware
 * synchronization with the radar or between separate sensors.
 */
motion_status_t motion_read(motion_sensor_t sensor, motion_sample_t *sample);
