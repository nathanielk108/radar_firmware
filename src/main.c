
#include <stm32h7xx.h>

#include "platform/clock.h"
#include "platform/core.h"
#include "platform/debug_swo.h"
#include "platform/time.h"
#include "platform/mpu.h"
#include "platform/usb.h"
#include "platform/i2c.h"

#include "board/board_power.h"
#include "board/board_i2c.h"

#include "drivers/bq25883.h"

#include "device/usbd.h"

int main(void)
{
    core_enable_faults();

    if (clock_init() != CLOCK_OK) {
        while (1) {}
    }

    mpu_init();
    core_enable_icache();
    core_enable_dcache();
    core_enable_cycle_counter();

    debug_swo_init();
    debug_swo_write("BOOT\r\n");

    if (!time_init()) {
        debug_swo_write("Time initialization failed\r\n");
        while (1) {}
    }

    board_power_init();

    if (!usb_init()) {
        debug_swo_write("USB initialization failed\r\n");
        while (1) {}
    }

    board_i2c_init();

    if (i2c_init(&batt_i2c_config) != I2C_STATUS_OK) {
        debug_swo_write("Battery I2C initialization failed\r\n");
        while (1) {}
    }

    bq25883_t battery = {
        .i2c = &batt_i2c_config
    };

    uint8_t part_info = 0;
    bq25883_status_t status =
        bq25883_check_id(&battery, &part_info);

    debug_swo_write("BQ25883 ID: ");
    debug_swo_hex32(part_info);
    debug_swo_write("\r\n");

    if (status != BQ25883_OK) {
        debug_swo_write("BQ25883 ID check failed: ");
        debug_swo_hex32(status);
        debug_swo_write("\r\n");
        while (1) {
            tud_task();
        }
    }

    status = bq25883_prepare_for_bringup(&battery);

    if (status != BQ25883_OK) {
        debug_swo_write("BQ25883 configuration failed: ");
        debug_swo_hex32(status);
        debug_swo_write("\r\n");
        while (1) {
            tud_task();
        }
    }

    debug_swo_write("BQ25883 ready - charging disabled\r\n");

    bq25883_snapshot_t snapshot;
    uint32_t start = time_ms();

    uint32_t enable_charge_timer = time_ms();
    bool charge_enabled = false;

    while (1) {
        tud_task();

        if (time_expired(start, 1000)) {
            start = time_ms();

            status = bq25883_get_snapshot(&battery, &snapshot);

            if (status != BQ25883_OK) {
                debug_swo_write("Snapshot failed: ");
                debug_swo_hex32(status);
                debug_swo_write("\r\n");
                uint8_t reg = 0x15;
                uint8_t value = 0x0;
                i2c_write_read(battery.i2c, BQ25883_I2C_ADDRESS, &reg, 1U, &value, 1U, 20U);
                debug_swo_write("REG_ADC_CTRL = ");
                debug_swo_hex32(value);
                debug_swo_write("\r\n");
                continue;
            }

            debug_swo_write("\r\n--- BQ25883 ---\r\n");

            debug_swo_write("VBUS (mV): ");
            debug_swo_hex32(snapshot.vbus_mv);

            debug_swo_write("\r\nVBAT (mV): ");
            debug_swo_hex32(snapshot.vbat_mv);

            debug_swo_write("\r\nVSYS (mV): ");
            debug_swo_hex32(snapshot.vsys_mv);

            debug_swo_write("\r\nIBUS (mA): ");
            debug_swo_hex32((uint16_t)snapshot.ibus_ma);

            debug_swo_write("\r\nICHG (mA): ");
            debug_swo_hex32(snapshot.ichg_ma);

            debug_swo_write("\r\nTDIE (0.5 C): ");
            debug_swo_hex32(snapshot.tdie_half_c);

            debug_swo_write("\r\nSTATUS1: ");
            debug_swo_hex32(snapshot.status1);

            debug_swo_write("\r\nSTATUS2: ");
            debug_swo_hex32(snapshot.status2);

            debug_swo_write("\r\nNTC: ");
            debug_swo_hex32(snapshot.ntc);

            debug_swo_write("\r\nFAULT: ");
            debug_swo_hex32(snapshot.fault);

            debug_swo_write("\r\nCHARGE STATE: ");
            debug_swo_hex32(snapshot.charge_state);

            debug_swo_write("\r\n----------------\r\n");
        }
        if (!charge_enabled && time_expired(enable_charge_timer, 5000)) {
          bq25883_set_charge_enabled(&battery, true);
          debug_swo_write("Enabled Charge");
          charge_enabled = true;
        }
    }
}

