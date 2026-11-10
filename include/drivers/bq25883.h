#pragma once

#include "platform/i2c.h"

#define BQ25883_ADDR 0x6BU

#pragma once

#include "platform/i2c.h"

#include <stdbool.h>
#include <stdint.h>

#define BQ25883_I2C_ADDRESS       0x6BU

#define BQ25883_REG_VREG          0x00U
#define BQ25883_REG_ICHG          0x01U
#define BQ25883_REG_VINDPM        0x02U
#define BQ25883_REG_IINDPM        0x03U
#define BQ25883_REG_PRE_TERM      0x04U
#define BQ25883_REG_CTRL1         0x05U
#define BQ25883_REG_CTRL2         0x06U
#define BQ25883_REG_CTRL3         0x07U
#define BQ25883_REG_CTRL4         0x08U

#define BQ25883_REG_STATUS1       0x0BU
#define BQ25883_REG_STATUS2       0x0CU
#define BQ25883_REG_NTC           0x0DU
#define BQ25883_REG_FAULT         0x0EU

#define BQ25883_REG_ADC_CTRL      0x15U
#define BQ25883_REG_ADC_DISABLE   0x16U

#define BQ25883_REG_IBUS_H        0x17U
#define BQ25883_REG_IBUS_L        0x18U
#define BQ25883_REG_ICHG_H        0x19U
#define BQ25883_REG_ICHG_L        0x1AU
#define BQ25883_REG_VBUS_H        0x1BU
#define BQ25883_REG_VBUS_L        0x1CU
#define BQ25883_REG_VBAT_H        0x1DU
#define BQ25883_REG_VBAT_L        0x1EU
#define BQ25883_REG_VSYS_H        0x1FU
#define BQ25883_REG_VSYS_L        0x20U
#define BQ25883_REG_TS_H          0x21U
#define BQ25883_REG_TS_L          0x22U
#define BQ25883_REG_TDIE_H        0x23U
#define BQ25883_REG_TDIE_L        0x24U

#define BQ25883_REG_PART_INFO     0x25U

typedef enum {
    BQ25883_OK = 0,
    BQ25883_ERROR_I2C,
    BQ25883_ERROR_ID,
    BQ25883_ERROR_RANGE,
    BQ25883_ERROR_TIMEOUT,
    BQ25883_ERROR_VERIFY,
    BQ25883_ERROR_SAFETY
} bq25883_status_t;

typedef enum {
    BQ25883_CHARGE_NONE      = 0,
    BQ25883_CHARGE_TRICKLE   = 1,
    BQ25883_CHARGE_PRECHARGE = 2,
    BQ25883_CHARGE_FAST      = 3,
    BQ25883_CHARGE_TAPER     = 4,
    BQ25883_CHARGE_TOPOFF    = 5,
    BQ25883_CHARGE_DONE      = 6
} bq25883_charge_state_t;

typedef struct {
    const i2c_config_t *i2c;
} bq25883_t;

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

    int16_t vbus_mv;
    int16_t vbat_mv;
    int16_t vsys_mv;
    uint16_t ichg_ma;
    int16_t ibus_ma;
    int16_t tdie_half_c;
} bq25883_snapshot_t;

bq25883_status_t bq25883_check_id(const bq25883_t *dev, uint8_t *part_info);
bq25883_status_t bq25883_set_charge_enabled(const bq25883_t *dev, bool enable);
bq25883_status_t bq25883_set_otg_enabled(const bq25883_t *dev, bool enable);
bq25883_status_t bq25883_disable_watchdog(const bq25883_t *dev);
bq25883_status_t bq25883_set_charge_voltage_mv(const bq25883_t *dev, uint16_t mv);
bq25883_status_t bq25883_set_charge_current_ma(const bq25883_t *dev, uint16_t ma);
bq25883_status_t bq25883_set_input_current_limit_ma(const bq25883_t *dev, uint16_t ma);
bq25883_status_t bq25883_set_precharge_termination_ma(const bq25883_t *dev, uint16_t precharge_ma, uint16_t termination_ma);
bq25883_status_t bq25883_adc_convert(const bq25883_t *dev, uint32_t timeout_ms);
bq25883_status_t bq25883_get_snapshot(const bq25883_t *dev, bq25883_snapshot_t *s);
bq25883_status_t bq25883_prepare_for_bringup(const bq25883_t *dev);
