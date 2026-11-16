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

#include "stm32h7xx_ll_tim.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* 0 = PLL unprogrammed, 1 = CW, 2 = hardware sawtooth ramp. */
#define RADAR_PLL_MODE 0
#define DIAGNOSTIC_PERIOD_MS 1000U
/* Optional repeated-DMA bring-up test; set to 100 after validating one read. */
#define ADC_DMA_SELF_TEST_COUNT 0U

/* TIM16 + DMA2 prototype: 64 paired conversions, then stop and dump USB CSV. */
#define ADC_TIMED_TEST_RATE_HZ 400000U
#define ADC_TIMED_TEST_SAMPLES 100U
#define ADC_TIMED_TEST_PERIOD_MS 2000U

static bool usb_ready;
static bool adc_bus_ready;
static bool adc_timed_ready;
static bool charger_bus_ready;
static bool imu1_bus_ready;
static bool imu2_bus_ready;

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
  bq25883_status_t rc = bq25883_prepare_for_bringup();
  if (rc == BQ25883_OK) {
    LOG_INFO("BQ25883: initialized");
    bq25883_snapshot_t snap = {0};
    bq25883_status_t snapshot_status = bq25883_get_snapshot(&snap);
    if (snapshot_status != BQ25883_OK) {
      /* CE remains inhibited if battery measurement fails. */
      LOG_ERROR("BQ25883: snapshot failed: %s",
                bq25883_status_name(snapshot_status));
      return;
    }

    bool charge = snap.vbat_mv >= 6400U;
    bq25883_status_t enable_status = bq25883_set_charge_enabled(charge);
    if (enable_status != BQ25883_OK) {
      LOG_ERROR("BQ25883: charge configuration failed: %s",
                bq25883_status_name(enable_status));
      return;
    }
    LOG_INFO("BQ25883: %s", charge ? "charge enabled"
                                  : "battery missing; charge disabled");
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
  bool spi_ready = rc == SPI_STATUS_OK;
  ad4683_status_t adc_rc = spi_ready ? ad4683_init() : AD4683_ERROR_SPI;
  if (adc_rc == AD4683_OK) {
    /* First output word is from the preceding conversion. */
    ad4683_sample_t discard;
    adc_rc = ad4683_read_sample(&discard);
  }
  adc_bus_ready = adc_rc == AD4683_OK;
  if (adc_bus_ready)
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
    LOG_INFO("%s", bq25883_diagnostic(line, sizeof(line)));
  /* Do not use the polling ADC driver while TIM16 owns PF6 and SPI4/5. */
  if (adc_bus_ready && !adc_timed_ready)
    LOG_INFO("%s", ad4683_diagnostic(line, sizeof(line)));
  if (imu1_bus_ready)
    LOG_INFO("%s", ism330is_diagnostic(line, sizeof(line)));
  if (imu2_bus_ready)
    LOG_INFO("%s", bmi323_diagnostic(line, sizeof(line)));
  LOG_INFO("%s", adf4158_diagnostic(line, sizeof(line)));
}

#if ADC_DMA_SELF_TEST_COUNT > 0
static void dma_self_test(void) {
  ad4683_sample_t sample = {0};
  for (uint32_t i = 0U; i < ADC_DMA_SELF_TEST_COUNT; ++i) {
    service_io();
    acquisition_status_t status = acquisition_capture_one(&sample, 10U);
    if (status != ACQUISITION_OK) {
      LOG_ERROR("ADC DMA test: capture %lu failed: %s", (unsigned long)i,
                acquisition_status_name(status));
      return;
    }
    /* Conservative quiet time until hardware CS timing is measured. */
    time_delay_ms(1U);
  }
  LOG_INFO("ADC DMA test: %lu captures OK (last A=%d B=%d)",
           (unsigned long)ADC_DMA_SELF_TEST_COUNT, sample.a, sample.b);
}
#endif


/* Bounded capture: no USB formatting while DMA is running. The returned
 * pointer refers directly to noncacheable DMA SRAM; nothing is copied.
 * Stop before printing so the next DMA block cannot overwrite it.
 */
static void test_timed_adc(void) {
  if (!adc_timed_ready || !usb_console_connected())
    return;

  acquisition_status_t status = acquisition_timed_start();
  if (status != ACQUISITION_OK) {
    LOG_ERROR("ADC timed start: %s", acquisition_status_name(status));
    return;
  }

  acquisition_block_t block = {0};
  uint32_t started = time_ms();
  do {
    status = acquisition_timed_get_block(&block);
    if (status != ACQUISITION_BUSY)
      break;
  } while (!time_expired(started, 10U));

  acquisition_status_t stopped = acquisition_timed_stop();
  if (status == ACQUISITION_BUSY)
    status = ACQUISITION_TIMEOUT;
  if (status != ACQUISITION_OK || stopped != ACQUISITION_OK) {
    char line[200];
    LOG_INFO("%s", acquisition_diagnostic(line, 200));
    LOG_ERROR("ADC timed capture: %s, stop: %s",
              acquisition_status_name(status),
              acquisition_status_name(stopped));
    return;
  }

  /* Print data AFTER stopping, using CDC regardless of PRINTF_OUT setting. */
  char line[80];
  int length = snprintf(line, sizeof(line),
                        "# ADC block %lu, %lu SPS, %u pairs\r\n"
                        "index,adc_a,adc_b\r\n",
                        (unsigned long)block.sequence,
                        (unsigned long)ADC_TIMED_TEST_RATE_HZ,
                        (unsigned int)block.count);
  if (length > 0 && (size_t)length < sizeof(line))
    (void)usb_console_write_bytes(line, (size_t)length);
  for (uint16_t i = 0U; i < block.count; ++i) {
    length = snprintf(line, sizeof(line), "%u,%d,%d\r\n",
                      (unsigned int)i, (int)(int16_t)block.a[i],
                      (int)(int16_t)block.b[i]);
    if (length > 0 && (size_t)length < sizeof(line))
      (void)usb_console_write_bytes(line, (size_t)length);
    service_io();
  }
  (void)acquisition_timed_release_block(block.sequence);
}

int main(void) {
  init_system();
  LOG_INFO("RADAR boot");
  init_charger();
  init_imus();
  init_rf_board();
  init_adc();
  init_pll();

  bool dma_ready = false;
  if (adc_bus_ready) {
    acquisition_status_t init_status = acquisition_init();
    dma_ready = init_status == ACQUISITION_OK;
    if (!dma_ready)
      LOG_ERROR("ADC DMA init: %s", acquisition_status_name(init_status));
  }

#if ADC_DMA_SELF_TEST_COUNT > 0
  if (dma_ready)
    dma_self_test();
#endif

  if (dma_ready) {
    const acquisition_timed_config_t cfg = {
      .sample_rate_hz = ADC_TIMED_TEST_RATE_HZ,
      .samples_per_block = ADC_TIMED_TEST_SAMPLES
    };
    acquisition_status_t rc = acquisition_timed_init(&cfg);
    adc_timed_ready = rc == ACQUISITION_OK;
    if (adc_timed_ready)
      LOG_INFO("ADC timed DMA ready: %lu SPS, %u pairs/block",
               (unsigned long)cfg.sample_rate_hz,
               (unsigned int)cfg.samples_per_block);
    else
      LOG_ERROR("ADC timed init: %s", acquisition_status_name(rc));
  }

  uint32_t last_timed_test = time_ms();
  uint32_t last = time_ms();
  for (;;) {
    service_io();
    if (adc_timed_ready &&
        time_expired(last_timed_test, ADC_TIMED_TEST_PERIOD_MS)) {
      last_timed_test = time_ms();
      test_timed_adc();
    }
    if (time_expired(last, DIAGNOSTIC_PERIOD_MS)) {
      last = time_ms(); /* Skip missed periods; never print catch-up bursts. */
      if (dma_ready && !adc_timed_ready) {
        ad4683_sample_t sample = {0};
        acquisition_status_t status = acquisition_capture_one(&sample, 10U);
        if (status != ACQUISITION_OK) {
          LOG_ERROR("DMA capture: %s", acquisition_status_name(status));
          if (status == ACQUISITION_NOT_READY)
            dma_ready = false;
        } else {
          LOG_INFO("DMA capture: %d, %d", sample.a, sample.b);
        }
      }
      print_diagnostics();
    }
  }
}
