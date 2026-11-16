#include "board/board_display.h"
#include "board/board_freq.h"
#include "board/board_gain.h"
#include "board/board_power.h"
#include "board/board_spi.h"
#include "drivers/ad4683.h"
#include "drivers/adf4158.h"
#include "drivers/bmi323.h"
#include "drivers/ism330is.h"
#include "platform/clock.h"
#include "platform/core.h"
#include "platform/debug_swo.h"
#include "platform/mpu.h"
#include "platform/time.h"
#include "platform/usb.h"
#include "services/acquisition.h"
#include "services/chirp.h"
#include "services/usb_charge.h"
#include "stm32h7xx.h"
#include "system/log.h"

#include <stdio.h>

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

static bool init_acquisition(void) {
  if (spi_init(&adc_slave_spi_config) != SPI_STATUS_OK ||
      spi_init(&adc_master_spi_config) != SPI_STATUS_OK) {
    LOG_ERROR("AD4683: SPI init failed");
    return false;
  }
  ad4683_status_t adc = ad4683_init();
  if (adc != AD4683_OK) {
    LOG_ERROR("AD4683: %s", ad4683_status_name(adc));
    return false;
  }

  acquisition_status_t status = acquisition_init();
  if (status == ACQUISITION_OK)
    LOG_INFO("AD4683: DMA ready. Send c for one chirp.");
  else
    LOG_ERROR("AD4683 DMA: %s", acquisition_status_name(status));
  return status == ACQUISITION_OK;
}

static void run_chirp_test(void) {
  chirp_capture_t capture;
  acquisition_status_t status = chirp_capture_one(&capture);
  if (status != ACQUISITION_OK) {
    LOG_ERROR("Chirp capture: %s", acquisition_status_name(status));
    return;
  }

  /* Format the complete record after DMA stops so it can be queued atomically.
   */
  char csv[1536];
  int length = snprintf(
      csv, sizeof(csv),
      "# sample_rate_hz=%lu,step_hz=%lu,step_rate_hz=%lu,"
      "samples=%u,gain_mv=%u\r\nindex,adc_a,adc_b\r\n",
      (unsigned long)capture.sample_rate_hz, (unsigned long)capture.step_hz,
      (unsigned long)capture.step_rate_hz, (unsigned)capture.samples.count,
      (unsigned)board_gain_get_mv());
  if (length < 0 || (size_t)length >= sizeof(csv))
    return;
  size_t used = (size_t)length;
  for (uint16_t i = 0; i < capture.samples.count; ++i) {
    length =
        snprintf(csv + used, sizeof(csv) - used, "%u,%d,%d\r\n", (unsigned)i,
                 (int)capture.samples.a[i], (int)capture.samples.b[i]);
    if (length < 0 || (size_t)length >= sizeof(csv) - used)
      return;
    used += (size_t)length;
  }
  uint32_t start = time_ms();
  while (usb_console_connected() && !usb_console_write_bytes(csv, used)) {
    if (time_expired(start, 100U))
      return;
    usb_task();
    usb_charge_task();
  }
}

static void reply_gain(bool set, uint16_t millivolts) {
  char reply[72];
  int length;
  if (set && !board_gain_set_mv(millivolts)) {
    length = snprintf(reply, sizeof(reply), "GAIN_DAC range %u-%u mV\r\n",
                      BOARD_GAIN_MIN_MV, BOARD_GAIN_MAX_MV);
  } else {
    length = snprintf(reply, sizeof(reply), "GAIN_DAC %u mV (nominal)\r\n",
                      (unsigned)board_gain_get_mv());
  }
  if (length > 0 && (size_t)length < sizeof(reply))
    (void)usb_console_write_bytes(reply, (size_t)length);
}

static void service_console(bool adc_ready) {
  static bool reading_gain;
  static bool has_value;
  static uint16_t gain_mv;

  if (!usb_console_connected()) {
    reading_gain = false;
    return;
  }
  char command;
  if (usb_console_read(&command, 1U) != 1U)
    return;

  if (reading_gain) {
    if (command == '\r' || command == '\n') {
      reply_gain(has_value, gain_mv);
      reading_gain = false;
    } else if (command != ' ' || has_value) {
      has_value = true;
      if (command < '0' || command > '9' || gain_mv > BOARD_GAIN_MAX_MV)
        gain_mv = UINT16_MAX;
      else
        gain_mv = (uint16_t)(gain_mv * 10U + (unsigned)(command - '0'));
    }
  } else if (command == 'g') {
    reading_gain = true;
    has_value = false;
    gain_mv = 0U;
  } else if (command == 'c') {
    if (adc_ready)
      run_chirp_test();
    else
      LOG_ERROR("Chirp capture: ADC unavailable");
  }
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

  usb_charge_init();
  board_spi_imu1_init();
  board_spi_imu2_init();
  init_imus();

  board_freq_init();
  board_spi_adc_init();
  board_rf_power_on();
  board_gain_init();
  LOG_INFO("GAIN_DAC: %u mV nominal", BOARD_GAIN_DEFAULT_MV);
  adf4158_power_down();
  LOG_INFO("ADF4158: software power-down sent");
  bool adc_ready = init_acquisition();

  bool usb_ready = usb_init();
  if (!usb_ready)
    debug_swo_write("USB CDC init failed\r\n");
  for (;;) {
    debug_swo_poll();
    if (usb_ready)
      usb_task();
    usb_charge_task();
    if (usb_ready)
      service_console(adc_ready);
  }
}
