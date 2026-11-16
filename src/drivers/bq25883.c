#include "drivers/bq25883.h"
#include "board/board_i2c.h"
#include "board/board_power.h"
#include "platform/i2c.h"
#include "platform/time.h"

#define BQ25883_I2C_ADDRESS 0x6BU
#define BQ25883_REG_VREG 0x00U
#define BQ25883_REG_ICHG 0x01U
#define BQ25883_REG_IINDPM 0x03U
#define BQ25883_REG_PRE_TERM 0x04U
#define BQ25883_REG_CTRL1 0x05U
#define BQ25883_REG_CTRL2 0x06U
#define BQ25883_REG_STATUS1 0x0BU
#define BQ25883_REG_ADC_CTRL 0x15U
#define BQ25883_REG_IBUS_H 0x17U
#define BQ25883_REG_PART_INFO 0x25U

#define BQ25883_WATCHDOG_MASK (3U << 4)
#define BQ25883_OTG_ENABLE (1U << 7)
#define BQ25883_CHARGE_ENABLE (1U << 3)
#define BQ25883_ADC_ENABLE (1U << 7)
#define BQ25883_ADC_ONE_SHOT (1U << 6)

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
    return BQ25883_ERROR_I2C;
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
  case BQ25883_ERROR_ADC:
    return "ADC conversion did not start";
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

static bq25883_status_t bq25883_read(uint8_t reg, uint8_t *data,
                                     uint8_t count) {
  i2c_status_t result = i2c_write_read(&batt_i2c_config, BQ25883_I2C_ADDRESS,
                                       &reg, 1U, data, count, 20U);

  return map_i2c_status(result);
}

static bq25883_status_t bq25883_read_reg(uint8_t reg, uint8_t *value) {
  return bq25883_read(reg, value, 1U);
}

static bq25883_status_t bq25883_write_reg(uint8_t reg, uint8_t value) {
  uint8_t data[2] = {reg, value};

  i2c_status_t result =
      i2c_write(&batt_i2c_config, BQ25883_I2C_ADDRESS, data, 2U, 20U);

  return map_i2c_status(result);
}

static bq25883_status_t bq25883_write_verify(uint8_t reg, uint8_t value,
                                             uint8_t mask) {
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

static bq25883_status_t bq25883_update_bits(uint8_t reg, uint8_t mask,
                                            uint8_t value) {
  uint8_t current;
  bq25883_status_t status = bq25883_read_reg(reg, &current);
  if (status != BQ25883_OK)
    return status;

  uint8_t updated = (uint8_t)((current & (uint8_t)~mask) | (value & mask));
  return bq25883_write_verify(reg, updated, mask);
}

bq25883_status_t bq25883_check_id(uint8_t *part_info) {
  uint8_t value;

  bq25883_status_t status = bq25883_read_reg(BQ25883_REG_PART_INFO, &value);

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

  bq25883_status_t status =
      bq25883_update_bits(BQ25883_REG_CTRL2, BQ25883_CHARGE_ENABLE,
                          enable ? BQ25883_CHARGE_ENABLE : 0U);
  if (status != BQ25883_OK)
    return status;

  if (enable)
    board_battery_charge_inhibit(false);
  return BQ25883_OK;
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

  return bq25883_update_bits(BQ25883_REG_ICHG, 0x3FU, (uint8_t)(ma / 50U));
}

bq25883_status_t bq25883_set_input_current_limit_ma(uint16_t ma) {
  if ((ma < 500U) || (ma > 3300U)) {
    return BQ25883_ERROR_RANGE;
  }

  uint8_t code = (uint8_t)((ma - 500U) / 100U);
  return bq25883_update_bits(BQ25883_REG_IINDPM, 0x1FU, code);
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

static bq25883_status_t bq25883_adc_sample(void) {
  bq25883_status_t status = bq25883_write_reg(
      BQ25883_REG_ADC_CTRL, BQ25883_ADC_ENABLE | BQ25883_ADC_ONE_SHOT);
  if (status != BQ25883_OK)
    return status;

  /* A one-shot request is ignored when both supplies are below UVLO. */
  uint8_t adc_control;
  status = bq25883_read_reg(BQ25883_REG_ADC_CTRL, &adc_control);
  if (status != BQ25883_OK)
    return status;
  if ((adc_control & BQ25883_ADC_ENABLE) == 0U)
    return BQ25883_ERROR_ADC;

  uint32_t start = time_ms();
  do {
    time_delay_ms(1U);
    status = bq25883_read_reg(BQ25883_REG_ADC_CTRL, &adc_control);
    if (status != BQ25883_OK)
      return status;
    if ((adc_control & BQ25883_ADC_ENABLE) == 0U)
      return BQ25883_OK;
  } while (!time_expired(start, 250U));
  return BQ25883_ERROR_TIMEOUT;
}

static uint16_t adc_word(const uint8_t *data) {
  return ((uint16_t)data[0] << 8) | data[1];
}

bq25883_status_t bq25883_get_snapshot(bq25883_snapshot_t *snapshot) {
  if (!snapshot)
    return BQ25883_ERROR_INVALID_ARGUMENT;
  bq25883_status_t status = bq25883_adc_sample();
  if (status != BQ25883_OK)
    return status;

  uint8_t flags[4];
  status = bq25883_read(BQ25883_REG_STATUS1, flags, sizeof(flags));
  if (status != BQ25883_OK)
    return status;

  uint8_t adc[14]; /* IBUS through TDIE, high byte first in each pair. */
  status = bq25883_read(BQ25883_REG_IBUS_H, adc, sizeof(adc));
  if (status != BQ25883_OK)
    return status;

  *snapshot = (bq25883_snapshot_t){
      .status1 = flags[0],
      .status2 = flags[1],
      .ntc = flags[2],
      .fault = flags[3],
      .charge_state = (bq25883_charge_state_t)(flags[0] & 0x07U),
      .watchdog_expired = (flags[0] & (1U << 3)) != 0U,
      .thermal_regulation = (flags[0] & (1U << 4)) != 0U,
      .input_voltage_limited = (flags[0] & (1U << 5)) != 0U,
      .input_current_limited = (flags[0] & (1U << 6)) != 0U,
      .power_good = (flags[1] & (1U << 7)) != 0U,
      .ts_status = flags[2] & 0x07U,
      .ibus_ma = (int16_t)adc_word(&adc[0]),
      .ichg_ma = adc_word(&adc[2]),
      .vbus_mv = adc_word(&adc[4]),
      .vbat_mv = adc_word(&adc[6]),
      .vsys_mv = adc_word(&adc[8]),
      .tdie_half_c = (int16_t)adc_word(&adc[12]),
  };
  return BQ25883_OK;
}

bq25883_status_t bq25883_init(void) {
  board_battery_charge_inhibit(true);
  bq25883_status_t status = bq25883_check_id(NULL);
  if (status != BQ25883_OK)
    return status;

  status = bq25883_update_bits(BQ25883_REG_CTRL2,
                               BQ25883_OTG_ENABLE | BQ25883_CHARGE_ENABLE, 0U);
  if (status != BQ25883_OK)
    return status;
  status = bq25883_update_bits(BQ25883_REG_CTRL1, BQ25883_WATCHDOG_MASK, 0U);
  if (status != BQ25883_OK)
    return status;

  /* Retain the bring-up limits with charging disabled. */
  status = bq25883_set_input_current_limit_ma(500U);
  if (status != BQ25883_OK)
    return status;
  status = bq25883_set_charge_current_ma(500U);
  if (status != BQ25883_OK)
    return status;
  status = bq25883_set_precharge_termination_ma(50U, 50U);
  if (status != BQ25883_OK)
    return status;
  status = bq25883_set_charge_voltage_mv(8200U);
  if (status != BQ25883_OK)
    return status;
  return bq25883_write_verify(BQ25883_REG_ADC_CTRL, BQ25883_ADC_ONE_SHOT,
                              0xF0U);
}
