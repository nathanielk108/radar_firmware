#include "platform/clock.h"
#include "platform/core.h"
#include "platform/debug_swo.h"
#include "platform/i2c.h"
#include "platform/mpu.h"
#include "platform/time.h"
#include "platform/usb.h"

#include "board/board_i2c.h"
#include "board/board_spi.h"
#include "board/board_power.h"
#include "board/board_freq.h"

#include "drivers/adf4158.h"
#include "drivers/ad4683.h"
#include "drivers/bq25883.h"

#include "printf.h"

int main(void) {
  core_enable_faults();

  if (clock_init() != CLOCK_OK) {
    while (1) { }
  }

  mpu_init();
  core_enable_icache();
  core_enable_dcache();
  core_enable_cycle_counter();

  debug_swo_init();

  if (!time_init()) {
    while (1) { }
  }

  board_power_init();
  board_i2c_batt_init();

  if (!usb_init()) {
      printf("USB initialization failed\r\n");
      while (1) { }
  }

  if (i2c_init(&batt_i2c_config) != I2C_STATUS_OK) {
    printf("Battery I2C initialization failed\r\n");
    while (1) { }
  }

  bq25883_t battery = {.i2c = &batt_i2c_config};

  uint8_t part_info = 0;
  bq25883_status_t batt_status = bq25883_check_id(&battery, &part_info);

  printf("BQ25883 ID: %0X\r\n", part_info);

  if (batt_status != BQ25883_OK) {
    printf("BQ25883 ID check failed: %0X\r\n", batt_status);
    while (1) { }
  }

  batt_status = bq25883_prepare_for_bringup(&battery);

  if (batt_status != BQ25883_OK) {
    printf("BQ25883 configuration failed: %0X\r\n", batt_status);
    while (1) { }
  }

  uint32_t start = time_ms();
  bq25883_set_charge_enabled(&battery, true);
  printf("BQ25883 ready - charging enabled\r\n");

  start = time_ms();
  while (1) {
    usb_task();
    if (time_expired(start, 1000U)) {
      start = time_ms();

      bq25883_print_snapshot(&battery);
    }
  }
}
