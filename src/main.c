#include "board/board_display.h"
#include "board/board_freq.h"
#include "board/board_i2c.h"
#include "board/board_power.h"
#include "board/board_spi.h"
#include "drivers/ad4683.h"
#include "drivers/adf4158.h"
#include "drivers/bmi323.h"
#include "drivers/bq25883.h"
#include "drivers/ism330is.h"
#include "platform/clock.h"
#include "platform/core.h"
#include "platform/debug_swo.h"
#include "platform/mpu.h"
#include "platform/time.h"
#include "platform/usb.h"
#include "services/acquisition.h"
#include "stm32h7xx.h"
#include "system/log.h"

static void init_charger(void) {
  if (i2c_init(&batt_i2c_config) != I2C_STATUS_OK) {
    LOG_ERROR("BQ25883: I2C init failed");
    return;
  }
  bq25883_status_t status = bq25883_init();
  if (status == BQ25883_OK)
    LOG_INFO("BQ25883: configured with charging inhibited");
  else
    LOG_ERROR("BQ25883: %s", bq25883_status_name(status));
}

static void init_imus(void) {
  ism330is_status_t imu1 = spi_init(&imu1_spi_config) == SPI_STATUS_OK
                               ? ism330is_init()
                               : ISM330IS_ERROR_SPI;
  if (imu1 == ISM330IS_OK)
    LOG_INFO("ISM330IS: ready");
  else
    LOG_ERROR("ISM330IS: %s", ism330is_status_name(imu1));

  bmi323_status_t imu2 = spi_init(&imu2_spi_config) == SPI_STATUS_OK
                             ? bmi323_init()
                             : BMI323_ERROR_SPI;
  if (imu2 == BMI323_OK)
    LOG_INFO("BMI323: ready");
  else
    LOG_ERROR("BMI323: %s", bmi323_status_name(imu2));
}

static void init_acquisition(void) {
  if (spi_init(&adc_slave_spi_config) != SPI_STATUS_OK ||
      spi_init(&adc_master_spi_config) != SPI_STATUS_OK) {
    LOG_ERROR("AD4683: SPI init failed");
    return;
  }
  ad4683_status_t adc = ad4683_init();
  if (adc != AD4683_OK) {
    LOG_ERROR("AD4683: %s", ad4683_status_name(adc));
    return;
  }

  const acquisition_timed_config_t config = {
      .sample_rate_hz = 400000U,
      .samples_per_block = 100U,
  };
  acquisition_status_t status = acquisition_init();
  if (status == ACQUISITION_OK)
    status = acquisition_timed_init(&config);
  if (status == ACQUISITION_OK)
    LOG_INFO("AD4683: timed DMA ready");
  else
    LOG_ERROR("AD4683 DMA: %s", acquisition_status_name(status));
}

static bool init_mcu(void) {
  core_enable_faults();
  core_enable_cycle_counter();
  clock_status_t status = clock_init();
  debug_swo_init();
  if (status != CLOCK_OK) {
    debug_swo_write("Clock init failed: ");
    debug_swo_hex32((uint32_t)status);
    debug_swo_write("\r\n");
    return false;
  }
  mpu_init();
  core_enable_icache();
  core_enable_dcache();
  if (!time_init()) {
    debug_swo_write("SysTick init failed\r\n");
    return false;
  }
  return true;
}

int main(void) {
  board_power_init();
  board_display_init();
  if (!init_mcu()) {
    debug_swo_flush_bounded(10000U);
    for (;;)
      __WFI();
  }

  LOG_INFO("RADAR boot");

  board_i2c_batt_init();
  init_charger();
  board_spi_imu1_init();
  board_spi_imu2_init();
  init_imus();

  board_freq_init();
  board_spi_adc_init();
  board_rf_power_on();
  adf4158_power_down();
  LOG_INFO("ADF4158: software power-down sent");
  init_acquisition();

  bool usb_ready = usb_init();
  if (!usb_ready)
    debug_swo_write("USB CDC init failed\r\n");
  for (;;) {
    debug_swo_poll();
    if (usb_ready)
      usb_task();
  }
}
