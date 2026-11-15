#include "drivers/bq25883.h"
#include "board/board_power.h"
#include "board/board_i2c.h"
#include "platform/i2c.h"
#include <stdio.h>

static bq25883_status_t map_i2c_status(i2c_status_t status) {
  switch (status) {
  case I2C_STATUS_OK:
    return BQ25883_OK;
  case I2C_STATUS_INVALID_ARGUMENT:
    return BQ25883_ERROR_INVALID_ARGUMENT;
  case I2C_STATUS_TIMEOUT:
    return BQ25883_ERROR_TIMEOUT;
  case I2C_STATUS_NACK:
    return BQ25883_ERROR_NACK;
  case I2C_STATUS_BUS_ERROR:
    return BQ25883_ERROR_BUS;
  case I2C_STATUS_ARBITRATION_LOST:
    return BQ25883_ERROR_ARBITRATION;
  case I2C_STATUS_OVERRUN:
    return BQ25883_ERROR_OVERRUN;
  default:
    return BQ25883_ERROR_INVALID_ARGUMENT;
  }
}

const char *bq25883_status_name(bq25883_status_t status) {
  switch (status) {
  case BQ25883_OK:
    return "OK";
  case BQ25883_ERROR_I2C:
    return "I2C error";
  case BQ25883_ERROR_ID:
    return "device ID mismatch";
  case BQ25883_ERROR_RANGE:
    return "out of range";
  case BQ25883_ERROR_TIMEOUT:
    return "I2C timeout";
  case BQ25883_ERROR_VERIFY:
    return "register verify failed";
  case BQ25883_ERROR_SAFETY:
    return "safety check failed";
  case BQ25883_ERROR_INVALID_ARGUMENT:
    return "invalid argument";
  case BQ25883_ERROR_NACK:
    return "I2C NACK";
  case BQ25883_ERROR_BUS:
    return "I2C bus error";
  case BQ25883_ERROR_ARBITRATION:
    return "I2C arbitration lost";
  case BQ25883_ERROR_OVERRUN:
    return "I2C overrun";
  default:
    return "unknown";
  }
}

static bq25883_status_t bq25883_read_reg(uint8_t reg,
                                         uint8_t *value) {
  if (value == NULL) {
    return BQ25883_ERROR_INVALID_ARGUMENT;
  }

  i2c_status_t result =
      i2c_write_read(&batt_i2c_config, BQ25883_I2C_ADDRESS, &reg, 1U, value, 1U, 20U);

  return map_i2c_status(result);
}

static bq25883_status_t bq25883_write_reg(uint8_t reg,
                                          uint8_t value) {
  uint8_t data[2] = {reg, value};

  i2c_status_t result = i2c_write(&batt_i2c_config, BQ25883_I2C_ADDRESS, data, 2U, 20U);

  return map_i2c_status(result);
}

static bq25883_status_t bq25883_write_verify(uint8_t reg,
                                             uint8_t value, uint8_t mask) {
  bq25883_status_t status = bq25883_write_reg(reg, value);
  if (status != BQ25883_OK)
    return status;

  uint8_t readback;
  status = bq25883_read_reg(reg, &readback);
  if (status != BQ25883_OK)
    return status;

  return ((readback & mask) == (value & mask)) ? BQ25883_OK
                                               : BQ25883_ERROR_VERIFY;
}

static bq25883_status_t bq25883_update_bits(uint8_t reg,
                                            uint8_t mask, uint8_t value) {
  uint8_t current;
  bq25883_status_t status = bq25883_read_reg(reg, &current);
  if (status != BQ25883_OK)
    return status;

  uint8_t updated = (uint8_t)((current & (uint8_t)~mask) | (value & mask));
  return bq25883_write_verify(reg, updated, mask);
}

bq25883_status_t bq25883_check_id(uint8_t *part_info) {
  uint8_t value;

  bq25883_status_t status =
      bq25883_read_reg(BQ25883_REG_PART_INFO, &value);

  if (status != BQ25883_OK) {
    return status;
  }

  if (part_info != NULL) {
    *part_info = value;
  }

  uint8_t part_number = (value >> 3U) & 0x0FU;

  if (part_number != 0x03U) {
    return BQ25883_ERROR_ID;
  }

  return BQ25883_OK;
}

bq25883_status_t bq25883_set_charge_enabled(bool enable) {
  /* Never assert active-low CE until the register write is verified. */
  board_battery_charge_inhibit(true);

  bq25883_status_t status = bq25883_update_bits(BQ25883_REG_CTRL2, 1U << 3,
                                                enable ? (1U << 3) : 0U);
  if (status != BQ25883_OK)
    return status;

  if (enable)
    board_battery_charge_inhibit(false);
  return BQ25883_OK;
}

bq25883_status_t bq25883_set_otg_enabled(bool enable) {
  return bq25883_update_bits(BQ25883_REG_CTRL2, 1U << 7,
                             enable ? (1U << 7) : 0U);
}

bq25883_status_t bq25883_disable_watchdog(void) {
  return bq25883_update_bits(BQ25883_REG_CTRL1, 0x30U, 0x00U);
}

bq25883_status_t bq25883_set_charge_voltage_mv(uint16_t mv) {
  if ((mv < 6800U) || (mv > 9200U)) {
    return BQ25883_ERROR_RANGE;
  }

  uint8_t code = (uint8_t)((mv - 6800U) / 10U);

  return bq25883_write_verify(BQ25883_REG_VREG, code, 0xFFU);
}

bq25883_status_t bq25883_set_charge_current_ma(uint16_t ma) {
  if ((ma < 100U) || (ma > 2200U)) {
    return BQ25883_ERROR_RANGE;
  }

  uint8_t reg;

  bq25883_status_t status = bq25883_read_reg(BQ25883_REG_ICHG, &reg);

  if (status != BQ25883_OK) {
    return status;
  }

  uint8_t code = (uint8_t)(ma / 50U);

  reg &= 0xC0U;
  reg |= code & 0x3FU;

  return bq25883_write_verify(BQ25883_REG_ICHG, reg, 0x3FU);
}

bq25883_status_t bq25883_set_input_current_limit_ma(uint16_t ma) {
  if ((ma < 500U) || (ma > 3300U)) {
    return BQ25883_ERROR_RANGE;
  }

  uint8_t reg;

  bq25883_status_t status = bq25883_read_reg(BQ25883_REG_IINDPM, &reg);

  if (status != BQ25883_OK) {
    return status;
  }

  uint8_t code = (uint8_t)((ma - 500U) / 100U);

  reg &= 0xE0U;
  reg |= code & 0x1FU;

  return bq25883_write_verify(BQ25883_REG_IINDPM, reg, 0x1FU);
}

bq25883_status_t bq25883_set_precharge_termination_ma(uint16_t precharge_ma,
                                                      uint16_t termination_ma) {
  if ((precharge_ma < 50U) || (precharge_ma > 800U) || (termination_ma < 50U) ||
      (termination_ma > 800U)) {
    return BQ25883_ERROR_RANGE;
  }

  uint8_t pre = (uint8_t)((precharge_ma - 50U) / 50U);

  uint8_t term = (uint8_t)((termination_ma - 50U) / 50U);

  uint8_t value = (uint8_t)((pre << 4U) | term);

  return bq25883_write_verify(BQ25883_REG_PRE_TERM, value, 0xFFU);
}

/* The ADC is free-running; enable once, not on every status read. */
bq25883_status_t bq25883_adc_convert(void) {
  return bq25883_update_bits(BQ25883_REG_ADC_CTRL, 0xF0U,
                             0x80U); /* continuous, 15-bit */
}

static bq25883_status_t bq25883_read_adc16(uint8_t high_reg, uint16_t *value) {
  if (!value)
    return BQ25883_ERROR_INVALID_ARGUMENT;
  uint8_t raw[2];
  i2c_status_t status = i2c_write_read(&batt_i2c_config, BQ25883_I2C_ADDRESS, &high_reg,
                                       1U, raw, 2U, 20U);
  if (status != I2C_STATUS_OK)
    return map_i2c_status(status);
  *value = ((uint16_t)raw[0] << 8U) | raw[1];
  return BQ25883_OK;
}

bq25883_status_t bq25883_get_snapshot(bq25883_snapshot_t *s) {
  if (s == NULL) {
    return BQ25883_ERROR_INVALID_ARGUMENT;
  }

  bq25883_status_t status;

  status = bq25883_read_reg(BQ25883_REG_STATUS1, &s->status1);
  if (status != BQ25883_OK)
    return status;

  status = bq25883_read_reg(BQ25883_REG_STATUS2, &s->status2);
  if (status != BQ25883_OK)
    return status;

  status = bq25883_read_reg(BQ25883_REG_NTC, &s->ntc);
  if (status != BQ25883_OK)
    return status;

  status = bq25883_read_reg(BQ25883_REG_FAULT, &s->fault);
  if (status != BQ25883_OK)
    return status;

  status = bq25883_read_adc16(BQ25883_REG_VBUS_H, &s->vbus_mv);
  if (status != BQ25883_OK)
    return status;

  status = bq25883_read_adc16(BQ25883_REG_VBAT_H, &s->vbat_mv);
  if (status != BQ25883_OK)
    return status;

  status = bq25883_read_adc16(BQ25883_REG_VSYS_H, &s->vsys_mv);
  if (status != BQ25883_OK)
    return status;

  uint16_t ichg;

  status = bq25883_read_adc16(BQ25883_REG_ICHG_H, &ichg);
  if (status != BQ25883_OK)
    return status;

  s->ichg_ma = (uint16_t)ichg;

  uint16_t ibus_raw;
  status = bq25883_read_adc16(BQ25883_REG_IBUS_H, &ibus_raw);
  if (status != BQ25883_OK)
    return status;
  s->ibus_ma = (int16_t)ibus_raw;

  status = bq25883_read_adc16(BQ25883_REG_TDIE_H, &s->tdie_half_c);
  if (status != BQ25883_OK)
    return status;

  s->charge_state = (bq25883_charge_state_t)(s->status1 & 0x07U);

  s->watchdog_expired = (s->status1 & (1U << 3)) != 0U;

  s->thermal_regulation = (s->status1 & (1U << 4)) != 0U;

  s->input_voltage_limited = (s->status1 & (1U << 5)) != 0U;

  s->input_current_limited = (s->status1 & (1U << 6)) != 0U;

  s->power_good = (s->status2 & (1U << 7)) != 0U;

  s->ts_status = s->ntc & 0x07U;

  return BQ25883_OK;
}

bq25883_status_t bq25883_prepare_for_bringup(void) {
  /* Hold the hardware inhibit before any I2C communication. */
  board_battery_charge_inhibit(true);
  bq25883_status_t status = bq25883_check_id(NULL);
  if (status != BQ25883_OK)
    return status;

  /*
   * First action: make sure charger and OTG are off.
   */
  status = bq25883_set_charge_enabled(false);
  if (status != BQ25883_OK)
    return status;

  status = bq25883_set_otg_enabled(false);
  if (status != BQ25883_OK)
    return status;

  /*
   * Prevent watchdog from silently restoring defaults.
   */
  status = bq25883_disable_watchdog();
  if (status != BQ25883_OK)
    return status;

  /*
   * Conservative initial limits.
   */
  status = bq25883_set_input_current_limit_ma(500);
  if (status != BQ25883_OK)
    return status;

  status = bq25883_set_charge_current_ma(500);
  if (status != BQ25883_OK)
    return status;

  status = bq25883_set_precharge_termination_ma(50, 50);
  if (status != BQ25883_OK)
    return status;

  /*
   * Bring-up value, not necessarily final pack value.
   */
  status = bq25883_set_charge_voltage_mv(8200);
  if (status != BQ25883_OK)
    return status;

  /*
   * Keep charging disabled.
   */
  status = bq25883_set_charge_enabled(false);
  if (status != BQ25883_OK)
    return status;
  return bq25883_adc_convert();
}

bool bq25883_power_good(void) { return board_battery_power_good(); }

const char *bq25883_diagnostic(char *buf, size_t size) {
  if (!buf || !size)
    return "BQ25883: invalid buffer";
  bq25883_snapshot_t data = {0};
  bq25883_status_t status = bq25883_get_snapshot(&data);
  if (status != BQ25883_OK) {
    (void)snprintf(buf, size, "BQ25883: %s", bq25883_status_name(status));
    return buf;
  }
  (void)snprintf(buf, size,
                 "BQ25883: VBUS=%umV VBAT=%umV VSYS=%umV IBUS=%dmA ICHG=%umA "
                 "TDIE=%u.%uC CHG=%u ST=%02X/%02X NTC=%02X FAULT=%02X CE=%s",
                 data.vbus_mv, data.vbat_mv, data.vsys_mv, data.ibus_ma,
                 data.ichg_ma, (unsigned)(data.tdie_half_c / 2U),
                 (unsigned)((data.tdie_half_c & 1U) ? 5U : 0U),
                 (unsigned)data.charge_state, data.status1, data.status2,
                 data.ntc, data.fault,
                 board_battery_charge_inhibited() ? "inhibited" : "enabled");
  return buf;
}
