#include "drivers/bq25883.h"
#include "platform/time.h"
#include "platform/gpio.h"
#include "board/board_pins.h"

static bq25883_status_t bq25883_read_reg(
    const bq25883_t *dev,
    uint8_t reg,
    uint8_t *value)
{
    if ((dev == NULL) || (dev->i2c == NULL) || (value == NULL)) {
        return BQ25883_ERROR_I2C;
    }

    i2c_status_t result =
        i2c_write_read(
            dev->i2c,
            BQ25883_I2C_ADDRESS,
            &reg,
            1U,
            value,
            1U,
            20U
        );

    return (result == I2C_STATUS_OK)
        ? BQ25883_OK
        : BQ25883_ERROR_I2C;
}

static bq25883_status_t bq25883_write_reg(
    const bq25883_t *dev,
    uint8_t reg,
    uint8_t value)
{
    uint8_t data[2] = {
        reg,
        value
    };

    i2c_status_t result =
        i2c_write(
            dev->i2c,
            BQ25883_I2C_ADDRESS,
            data,
            2U,
            20U
        );

    return (result == I2C_STATUS_OK)
        ? BQ25883_OK
        : BQ25883_ERROR_I2C;
}

static bq25883_status_t bq25883_write_verify(
    const bq25883_t *dev,
    uint8_t reg,
    uint8_t value,
    uint8_t mask)
{
    bq25883_status_t status =
        bq25883_write_reg(dev, reg, value);

    if (status != BQ25883_OK) {
        return status;
    }

    uint8_t readback;

    status = bq25883_read_reg(dev, reg, &readback);

    if (status != BQ25883_OK) {
        return status;
    }

    if ((readback & mask) != (value & mask)) {
        return BQ25883_ERROR_VERIFY;
    }

    return BQ25883_OK;
}


static bq25883_status_t bq25883_update_bits(
    const bq25883_t *dev,
    uint8_t reg,
    uint8_t mask,
    uint8_t value)
{
    uint8_t current;

    bq25883_status_t status =
        bq25883_read_reg(dev, reg, &current);

    if (status != BQ25883_OK) {
        return status;
    }

    uint8_t updated =
        (current & (uint8_t)~mask) | (value & mask);

    return bq25883_write_verify(
        dev, reg, updated, mask
    );
}

bq25883_status_t bq25883_check_id(
    const bq25883_t *dev,
    uint8_t *part_info)
{
    uint8_t value;

    bq25883_status_t status =
        bq25883_read_reg(
            dev,
            BQ25883_REG_PART_INFO,
            &value
        );

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

bq25883_status_t bq25883_set_charge_enabled(
    const bq25883_t *dev,
    bool enable)
{
    if (!enable) {
        gpio_write(BATT_CE_PORT, BATT_CE_PIN, true);
    }

    bq25883_status_t status = bq25883_update_bits(
        dev,
        BQ25883_REG_CTRL2,
        1U << 3,
        enable ? (1U << 3) : 0U
    );

    if (status != BQ25883_OK) {
        gpio_write(BATT_CE_PORT, BATT_CE_PIN, true);
        return status;
    }

    if (enable) {
        gpio_write(BATT_CE_PORT, BATT_CE_PIN, false);
    }

    return BQ25883_OK;
}

bq25883_status_t bq25883_set_otg_enabled(
    const bq25883_t *dev,
    bool enable)
{
    return bq25883_update_bits(
        dev,
        BQ25883_REG_CTRL2,
        1U << 7,
        enable ? (1U << 7) : 0U
    );
}

bq25883_status_t bq25883_disable_watchdog(
    const bq25883_t *dev)
{
    return bq25883_update_bits(
        dev,
        BQ25883_REG_CTRL1,
        0x30U,
        0x00U
    );
}

bq25883_status_t bq25883_set_charge_voltage_mv(
    const bq25883_t *dev,
    uint16_t mv)
{
    if ((mv < 6800U) || (mv > 9200U)) {
        return BQ25883_ERROR_RANGE;
    }

    uint8_t code =
        (uint8_t)((mv - 6800U) / 10U);

    return bq25883_write_reg(
        dev,
        BQ25883_REG_VREG,
        code
    );
}

bq25883_status_t bq25883_set_charge_current_ma(
    const bq25883_t *dev,
    uint16_t ma)
{
    if ((ma < 100U) || (ma > 2200U)) {
        return BQ25883_ERROR_RANGE;
    }

    uint8_t reg;

    bq25883_status_t status =
        bq25883_read_reg(dev, BQ25883_REG_ICHG, &reg);

    if (status != BQ25883_OK) {
        return status;
    }

    uint8_t code = (uint8_t)(ma / 50U);

    reg &= 0xC0U;
    reg |= code & 0x3FU;

    return bq25883_write_reg(
        dev,
        BQ25883_REG_ICHG,
        reg
    );
}

bq25883_status_t bq25883_set_input_current_limit_ma(
    const bq25883_t *dev,
    uint16_t ma)
{
    if ((ma < 500U) || (ma > 3300U)) {
        return BQ25883_ERROR_RANGE;
    }

    uint8_t reg;

    bq25883_status_t status =
        bq25883_read_reg(dev, BQ25883_REG_IINDPM, &reg);

    if (status != BQ25883_OK) {
        return status;
    }

    uint8_t code =
        (uint8_t)((ma - 500U) / 100U);

    reg &= 0xE0U;
    reg |= code & 0x1FU;

    return bq25883_write_reg(
        dev,
        BQ25883_REG_IINDPM,
        reg
    );
}

bq25883_status_t bq25883_set_precharge_termination_ma(
    const bq25883_t *dev,
    uint16_t precharge_ma,
    uint16_t termination_ma)
{
    if ((precharge_ma < 50U) ||
        (precharge_ma > 800U) ||
        (termination_ma < 50U) ||
        (termination_ma > 800U)) {
        return BQ25883_ERROR_RANGE;
    }

    uint8_t pre =
        (uint8_t)((precharge_ma - 50U) / 50U);

    uint8_t term =
        (uint8_t)((termination_ma - 50U) / 50U);

    uint8_t value =
        (uint8_t)((pre << 4U) | term);

    return bq25883_write_reg(
        dev,
        BQ25883_REG_PRE_TERM,
        value
    );
}

bq25883_status_t bq25883_adc_convert(
    const bq25883_t *dev,
    uint32_t timeout_ms)
{
    uint8_t ctrl;

    bq25883_status_t status =
        bq25883_read_reg(
            dev,
            BQ25883_REG_ADC_CTRL,
            &ctrl
        );

    if (status != BQ25883_OK) {
        return status;
    }

    /*
     * ADC_RATE = 1: one-shot
     * ADC_SAMPLE = 00: highest effective resolution
     * ADC_EN = 1
     */
    ctrl &= ~0x30U;
    ctrl |= (1U << 6);
    ctrl |= (1U << 7);

    status = bq25883_write_reg(
        dev,
        BQ25883_REG_ADC_CTRL,
        ctrl
    );

    if (status != BQ25883_OK) {
        return status;
    }

    uint32_t start = time_ms();

    do {
        status =
            bq25883_read_reg(
                dev,
                BQ25883_REG_ADC_CTRL,
                &ctrl
            );

        if (status != BQ25883_OK) {
            return status;
        }

        if ((ctrl & (1U << 7)) == 0U) {
            return BQ25883_OK;
        }

    } while (!time_expired(start, timeout_ms));

    return BQ25883_ERROR_TIMEOUT;
}

static bq25883_status_t bq25883_read_adc16(
    const bq25883_t *dev,
    uint8_t high_reg,
    uint16_t *value)
{
    uint8_t high;
    uint8_t low;

    bq25883_status_t status =
        bq25883_read_reg(dev, high_reg, &high);

    if (status != BQ25883_OK) {
        return status;
    }

    status = bq25883_read_reg(
        dev, (uint8_t)(high_reg + 1U), &low
    );

    if (status != BQ25883_OK) {
        return status;
    }

    *value = ((uint16_t)high << 8U) | low;

    return BQ25883_OK;
}

bq25883_status_t bq25883_get_snapshot(
    const bq25883_t *dev,
    bq25883_snapshot_t *s)
{
    if (s == NULL) {
        return BQ25883_ERROR_RANGE;
    }

    bq25883_status_t status;

    status = bq25883_read_reg(
        dev, BQ25883_REG_STATUS1, &s->status1);
    if (status != BQ25883_OK) return status;

    status = bq25883_read_reg(
        dev, BQ25883_REG_STATUS2, &s->status2);
    if (status != BQ25883_OK) return status;

    status = bq25883_read_reg(
        dev, BQ25883_REG_NTC, &s->ntc);
    if (status != BQ25883_OK) return status;

    status = bq25883_read_reg(
        dev, BQ25883_REG_FAULT, &s->fault);
    if (status != BQ25883_OK) return status;

    status = bq25883_adc_convert(dev, 100);
    if (status != BQ25883_OK) return status;

    status = bq25883_read_adc16(
        dev, BQ25883_REG_VBUS_H, &s->vbus_mv);
    if (status != BQ25883_OK) return status;

    status = bq25883_read_adc16(
        dev, BQ25883_REG_VBAT_H, &s->vbat_mv);
    if (status != BQ25883_OK) return status;

    status = bq25883_read_adc16(
        dev, BQ25883_REG_VSYS_H, &s->vsys_mv);
    if (status != BQ25883_OK) return status;

    int16_t ichg;

    status = bq25883_read_adc16(
        dev, BQ25883_REG_ICHG_H, &ichg);
    if (status != BQ25883_OK) return status;

    s->ichg_ma = (uint16_t)ichg;

    status = bq25883_read_adc16(
        dev, BQ25883_REG_IBUS_H, &s->ibus_ma);
    if (status != BQ25883_OK) return status;

    status = bq25883_read_adc16(
        dev, BQ25883_REG_TDIE_H,
        &s->tdie_half_c);
    if (status != BQ25883_OK) return status;

    s->charge_state =
        (bq25883_charge_state_t)(s->status1 & 0x07U);

    s->watchdog_expired =
        (s->status1 & (1U << 3)) != 0U;

    s->thermal_regulation =
        (s->status1 & (1U << 4)) != 0U;

    s->input_voltage_limited =
        (s->status1 & (1U << 5)) != 0U;

    s->input_current_limited =
        (s->status1 & (1U << 6)) != 0U;

    s->power_good =
        (s->status2 & (1U << 7)) != 0U;

    s->ts_status = s->ntc & 0x07U;

    return BQ25883_OK;
}

static bool bq25883_ts_allows_charging(uint8_t ts)
{
    return ts == 0x00U;
}

bq25883_status_t bq25883_prepare_for_bringup(
    const bq25883_t *dev)
{
    bq25883_status_t status;
    uint8_t part;

    status = bq25883_check_id(dev, &part);
    if (status != BQ25883_OK) return status;

    /*
     * First action: make sure charger and OTG are off.
     */
    status = bq25883_set_charge_enabled(dev, false);
    if (status != BQ25883_OK) return status;

    status = bq25883_set_otg_enabled(dev, false);
    if (status != BQ25883_OK) return status;

    /*
     * Prevent watchdog from silently restoring defaults.
     */
    status = bq25883_disable_watchdog(dev);
    if (status != BQ25883_OK) return status;

    /*
     * Conservative initial limits.
     */
    status = bq25883_set_input_current_limit_ma(dev, 500);
    if (status != BQ25883_OK) return status;

    status = bq25883_set_charge_current_ma(dev, 100);
    if (status != BQ25883_OK) return status;

    status =
        bq25883_set_precharge_termination_ma(
            dev, 50, 50);
    if (status != BQ25883_OK) return status;

    /*
     * Bring-up value, not necessarily final pack value.
     */
    status = bq25883_set_charge_voltage_mv(dev, 8200);
    if (status != BQ25883_OK) return status;

    /*
     * Keep charging disabled.
     */
    return bq25883_set_charge_enabled(dev, false);
}
