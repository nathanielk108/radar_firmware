#include "platform/clock.h"
#include "platform/core.h"
#include "platform/debug_swo.h"
#include "platform/i2c.h"
#include "platform/mpu.h"
#include "platform/spi.h"
#include "platform/time.h"
#include "platform/usb.h"

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

#include "system/log.h"

#include "services/acquisition.h"

#include <stdbool.h>
#include <stdint.h>

/* 0 = PLL unprogrammed, 1 = CW, 2 = hardware sawtooth ramp. */
#define RADAR_PLL_MODE 0
#define DIAGNOSTIC_PERIOD_MS 1000U

static bool usb_ready;
static bool adc_bus_ready;
static bool charger_bus_ready;
static bool imu1_bus_ready;
static bool imu2_bus_ready;

static const bq25883_t charger = {.i2c = &batt_i2c_config};

static void service_io(void) {
  debug_swo_poll();
  if (usb_ready)
    usb_task();
}

static void init_system(void) {
  core_enable_faults();
  core_enable_cycle_counter(); /* DWT timeouts before SysTick/clock switch. */
  if (clock_init() != CLOCK_OK)
    for (;;) {
    }
  mpu_init();
  core_enable_icache();
  core_enable_dcache();
  debug_swo_init();
  if (!time_init())
    for (;;) {
    }

  /* Safe pin levels precede any device or RF initialization. */
  board_power_init();
  board_i2c_batt_init();
  board_spi_imu1_init();
  board_spi_imu2_init();
  board_spi_adc_init();
  board_freq_init();
  board_display_init();
  usb_ready = usb_init();
}

static void init_charger(void) {
  charger_bus_ready = i2c_init(&batt_i2c_config) == I2C_STATUS_OK;
  if (!charger_bus_ready) {
    LOG_ERROR("BQ25883: I2C init failed");
    return;
  }
  bq25883_status_t rc = bq25883_prepare_for_bringup(&charger);
  if (rc == BQ25883_OK) {
    LOG_INFO("BQ25883: initialized");
    bq25883_snapshot_t snap;
    bq25883_get_snapshot(&charger, &snap);
    if (snap.vbat_mv >= 6400) {
      bq25883_set_charge_enabled(&charger, true);
      LOG_INFO("BQ25883: battery detected; charge enabled");
    } else {
      bq25883_set_charge_enabled(&charger, false);
      LOG_INFO("BQ25883: battery missing; charge disabled");
    }
  } else
    LOG_ERROR("BQ25883: init failed: %s", bq25883_status_name(rc));
}

static void init_imus(void) {
  spi_status_t transport = spi_init(&imu1_spi_config);
  imu1_bus_ready = transport == SPI_STATUS_OK;
  ism330is_status_t imu1_rc =
      imu1_bus_ready ? ism330is_init() : ISM330IS_ERROR_SPI;
  if (imu1_rc == ISM330IS_OK)
    LOG_INFO("ISM330IS: ID verified");
  else
    LOG_ERROR("ISM330IS: %s", ism330is_status_name(imu1_rc));

  transport = spi_init(&imu2_spi_config);
  imu2_bus_ready = transport == SPI_STATUS_OK;
  bmi323_status_t imu2_rc = imu2_bus_ready ? bmi323_init() : BMI323_ERROR_SPI;
  if (imu2_rc == BMI323_OK)
    LOG_INFO("BMI323: ID verified");
  else
    LOG_ERROR("BMI323: %s", bmi323_status_name(imu2_rc));
}

static void init_rf_board(void) {
  /* The AD4683 and its reference are on the RF board: BOTH rails required. */
  board_rf_5v_enable(true);
  time_delay_ms(200U);
  board_rf_15v_enable(true);
  time_delay_ms(200U);
}

static void init_adc(void) {
  spi_status_t rc = spi_init(&adc_slave_spi_config);
  if (rc == SPI_STATUS_OK)
    rc = spi_init(&adc_master_spi_config);
  adc_bus_ready = rc == SPI_STATUS_OK;
  ad4683_status_t adc_rc = adc_bus_ready ? ad4683_init() : AD4683_ERROR_SPI;
  if (adc_rc == AD4683_OK) {
    /* First output word is from the preceding conversion. */
    ad4683_sample_t discard;
    adc_rc = ad4683_read_sample(&discard);
  }
  if (adc_rc == AD4683_OK)
    LOG_INFO("AD4683: external reference selected");
  else
    LOG_ERROR("AD4683: %s", ad4683_status_name(adc_rc));
}

static void init_pll(void) {
#if RADAR_PLL_MODE == 1
  bool ok = adf4158_program_fixed(&board_adf4158_fixed);
  if (ok)
    LOG_INFO("ADF4158: CW programmed (lock unverified)");
  else
    LOG_ERROR("ADF4158: %s", adf4158_status_name(adf4158_last_status()));
#elif RADAR_PLL_MODE == 2
  adf4158_ramp_result_t result;
  bool ok = adf4158_program_ramp(&board_adf4158_ramp, &result);
  if (ok)
    LOG_INFO("ADF4158: ramp programmed (lock unverified)");
  else
    LOG_ERROR("ADF4158: %s", adf4158_status_name(adf4158_last_status()));
#endif
}

static void print_diagnostics(void) {
  char line[224];
  LOG_INFO("diagnostics");
  if (charger_bus_ready)
    LOG_INFO("%s", bq25883_diagnostic(&charger, line, sizeof(line)));
  if (adc_bus_ready)
    LOG_INFO("%s", ad4683_diagnostic(line, sizeof(line)));
  if (imu1_bus_ready)
    LOG_INFO("%s", ism330is_diagnostic(line, sizeof(line)));
  if (imu2_bus_ready)
    LOG_INFO("%s", bmi323_diagnostic(line, sizeof(line)));
  LOG_INFO("%s", adf4158_diagnostic(line, sizeof(line)));
}

int main(void) {
  init_system();
  LOG_INFO("RADAR boot");
  init_charger();
  init_imus();
  init_rf_board();
  init_adc();
  init_pll();

  acquisition_init();

  uint32_t last = time_ms();
  for (;;) {
    service_io();
    if (time_expired(last, DIAGNOSTIC_PERIOD_MS)) {
      last = time_ms(); /* Skip missed periods; never print catch-up bursts. */
      ad4683_sample_t sample = {0};
      acquisition_status_t status = acquisition_capture_one(&sample, 10);
      if (status != ACQUISITION_OK) {
        LOG_ERROR("DMA Capture Failed: %s", acquisition_status_name(status));
      } else {
        LOG_INFO("DMA Capture: %d, %d", sample.a, sample.b);
      }
      print_diagnostics();
    }
  }
}
