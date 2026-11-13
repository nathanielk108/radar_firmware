#include "platform/clock.h"
#include "platform/core.h"
#include "platform/debug_swo.h"
#include "platform/i2c.h"
#include "platform/mpu.h"
#include "platform/spi.h"
#include "platform/time.h"
#include "platform/usb.h"

#include "board/board_freq.h"
#include "board/board_i2c.h"
#include "board/board_power.h"
#include "board/board_spi.h"

#include "drivers/ad4683.h"
#include "drivers/bq25883.h"
#include "drivers/bmi323.h"
#include "drivers/ism330is.h"

#include "printf.h"

static void halt(void) {
  for (;;) {
    usb_task();
  }
}

static void system_init(void) {
  core_enable_faults();
  if (clock_init() != CLOCK_OK)
    halt();

  mpu_init();
  core_enable_icache();
  core_enable_dcache();
  core_enable_cycle_counter();
  debug_swo_init();
  if (!time_init())
    halt();

  board_power_init(); /* RF off, battery charging inhibited */
  board_i2c_batt_init();
  board_spi_adc_init();
  board_freq_init();
  if (!usb_init())
    halt();
}

static void battery_init(bq25883_t *battery) {
  if (i2c_init(&batt_i2c_config) != I2C_STATUS_OK) {
    printf("Battery I2C init failed\r\n");
    halt();
  }

  uint8_t id = 0;
  bq25883_status_t status = bq25883_check_id(battery, &id);
  if (status != BQ25883_OK) {
    printf("BQ25883 ID error %u (0x%02X)\r\n", (unsigned)status, id);
    halt();
  }
  status = bq25883_prepare_for_bringup(battery);
  if (status != BQ25883_OK) {
    printf("BQ25883 config error %u\r\n", (unsigned)status);
    halt();
  }
  printf("BQ25883 ready; charge inhibited\r\n");
}

static void rf_power_up(void) {
  /* ADC and reference live on the RF board; BOTH rails are required. */
  board_rf_15v_enable(true);
  board_rf_5v_enable(true);
}

static void adc_init(void) {
  spi_status_t status = spi_init(&adc_slave_spi_config);
  if (status == SPI_STATUS_OK)
    status = spi_init(&adc_master_spi_config);
  if (status == SPI_STATUS_OK)
    status = ad4683_init();
  if (status != SPI_STATUS_OK) {
    printf("AD4683 init error %u\r\n", (unsigned)status);
    halt();
  }

  /* The AD4683 returns the previous conversion on each read. */
  ad4683_sample_t discard;
  if (ad4683_read_sample(&discard) != SPI_STATUS_OK)
    halt();

  printf("AD4683 ready\r\n");
}

int main(void) {
  system_init();
  printf("BOOT\r\n");

  bq25883_t battery = {.i2c = &batt_i2c_config};
  battery_init(&battery);
  bq25883_set_charge_enabled(&battery, true);
  // rf_power_up();
  // adc_init();

  board_spi_imu1_init();
  board_spi_imu2_init();

  spi_status_t s1 = spi_init(&imu1_spi_config);
  spi_status_t s2 = spi_init(&imu2_spi_config);

  if (s1 != SPI_STATUS_OK || s2 != SPI_STATUS_OK) {
      printf("IMU SPI initialization failed\r\n");
      halt();
  }

  time_delay_ms(10);

  uint8_t ism_id = 0;
  uint16_t bmi_id = 0;

  uint32_t last = time_ms();
  for (;;) {
    usb_task();
    if (!time_expired(last, 1000U)) continue;
    last += 1000U;

    bq25883_snapshot_t snapshot;
    bq25883_get_snapshot(&battery, &snapshot);
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "VBAT (mV): %d\r\n", snapshot.vbat_mv);
    usb_console_write(buffer);

    s1 = ism330is_read_id(&ism_id);
    s2 = bmi323_read_id(&bmi_id);

    printf("ISM330IS: 0x%02X (status %u)\r\n", ism_id, s1);
    printf("BMI323:   0x%04X (status %u)\r\n", bmi_id, s2);
  }
}
