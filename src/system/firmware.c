#include "system/firmware.h"

#include "board/board_display.h"
#include "board/board_freq.h"
#include "board/board_gain.h"
#include "board/board_power.h"
#include "board/board_spi.h"
#include "drivers/ad4683.h"
#include "drivers/adf4158.h"
#include "platform/clock.h"
#include "platform/core.h"
#include "platform/debug_swo.h"
#include "platform/mpu.h"
#include "platform/time.h"
#include "platform/usb.h"
#include "services/acquisition.h"
#include "services/chirp.h"
#include "services/motion.h"
#include "services/storage.h"
#include "services/usb_charge.h"
#include "system/log.h"

static bool radar_ready;
static bool usb_ready;

_Static_assert(FIRMWARE_GAIN_MIN_MV == BOARD_GAIN_MIN_MV &&
                   FIRMWARE_GAIN_MAX_MV == BOARD_GAIN_MAX_MV,
               "Public gain limits must match board limits");

static bool capture_active(void) {
  return chirp_capture_active() || acquisition_active();
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
  if (status != ACQUISITION_OK) {
    LOG_ERROR("AD4683 DMA: %s", acquisition_status_name(status));
    return false;
  }
  return true;
}

bool firmware_rf_enable(bool enable) {
  if (capture_active())
    return false;
  if (enable == radar_ready)
    return true;
  if (enable) {
    board_rf_power_on();
    adf4158_power_down();
    if (init_acquisition()) {
      board_gain_init();
      radar_ready = true;
      LOG_INFO("RADAR ADC ready; PLL in software power-down");
      return true;
    }
  } else {
    (void)acquisition_deinit();
    adf4158_power_down();
    board_gain_disable();
  }
  board_rf_power_off();
  radar_ready = false;
  LOG_INFO("RF rails off");
  return !enable;
}

bool firmware_gain_get(uint16_t *millivolts) {
  if (!radar_ready || !millivolts)
    return false;
  *millivolts = board_gain_get_mv();
  return true;
}

bool firmware_gain_set(uint16_t millivolts) {
  return radar_ready && !capture_active() && board_gain_set_mv(millivolts);
}

bool firmware_init(void) {
  board_power_init();
  board_display_init();
  if (!init_mcu())
    return false;

  LOG_INFO("RADAR boot; send help over USB CDC");
  usb_charge_init();
  usb_ready = usb_init();
  if (!usb_ready)
    LOG_ERROR("USB CDC init failed");

  motion_init();
  LOG_INFO("IMUs ISM330IS=%s BMI323=%s",
           motion_ready(MOTION_ISM330IS) ? "ready" : "unavailable",
           motion_ready(MOTION_BMI323) ? "ready" : "unavailable");

  storage_init();
  if (storage_present()) {
    storage_status_t status = storage_mount();
    LOG_INFO("SD mount: %s", storage_status_name(status));
  }

  board_freq_init();
  board_spi_adc_init();
  (void)firmware_rf_enable(true);
  return true;
}

void firmware_poll(void) {
  debug_swo_poll();
  if (usb_ready)
    usb_task();
  usb_charge_task();
  storage_poll();
}

firmware_status_t firmware_status(void) {
  return (firmware_status_t){.usb_ready = usb_ready,
                             .vbus_present = board_battery_power_good(),
                             .charge_inhibited = board_battery_charge_inhibited(),
                             .efuse_power_good = board_efuse_power_good(),
                             .radar_ready = radar_ready};
}
