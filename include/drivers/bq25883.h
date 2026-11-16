#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  BQ25883_OK = 0,
  BQ25883_ERROR_I2C,
  BQ25883_ERROR_ID,
  BQ25883_ERROR_RANGE,
  BQ25883_ERROR_TIMEOUT,
  BQ25883_ERROR_VERIFY,
  BQ25883_ERROR_ADC,
  BQ25883_ERROR_INVALID_ARGUMENT,
  BQ25883_ERROR_NACK,
  BQ25883_ERROR_BUS,
  BQ25883_ERROR_ARBITRATION,
  BQ25883_ERROR_OVERRUN
} bq25883_status_t;

typedef enum {
  BQ25883_CHARGE_NONE = 0,
  BQ25883_CHARGE_TRICKLE = 1,
  BQ25883_CHARGE_PRECHARGE = 2,
  BQ25883_CHARGE_FAST = 3,
  BQ25883_CHARGE_TAPER = 4,
  BQ25883_CHARGE_TOPOFF = 5,
  BQ25883_CHARGE_DONE = 6
} bq25883_charge_state_t;

typedef struct {
  uint8_t status1;
  uint8_t status2;
  uint8_t ntc;
  uint8_t fault;

  bq25883_charge_state_t charge_state;

  bool power_good;
  bool input_current_limited;
  bool input_voltage_limited;
  bool thermal_regulation;
  bool watchdog_expired;

  uint8_t ts_status;

  uint16_t vbus_mv;
  uint16_t vbat_mv;
  uint16_t vsys_mv;
  uint16_t ichg_ma;
  int16_t ibus_ma;
  int16_t tdie_half_c;
} bq25883_snapshot_t;

/* Configures the charger with CE inhibited. */
bq25883_status_t bq25883_init(void);
bq25883_status_t bq25883_check_id(uint8_t *part_info);
bq25883_status_t bq25883_set_charge_enabled(bool enable);
bq25883_status_t bq25883_set_charge_voltage_mv(uint16_t mv);
bq25883_status_t bq25883_set_charge_current_ma(uint16_t ma);
bq25883_status_t bq25883_set_input_current_limit_ma(uint16_t ma);
bq25883_status_t bq25883_set_precharge_termination_ma(uint16_t precharge_ma,
                                                      uint16_t termination_ma);
/* Starts and waits for a fresh one-shot ADC conversion. */
bq25883_status_t bq25883_get_snapshot(bq25883_snapshot_t *s);

const char *bq25883_status_name(bq25883_status_t status);
