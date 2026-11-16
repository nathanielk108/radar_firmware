#include "services/usb_charge.h"

#include "board/board_i2c.h"
#include "board/board_power.h"
#include "board/board_usb_cc.h"
#include "drivers/bq25883.h"
#include "platform/time.h"
#include "platform/usb.h"
#include "system/log.h"

#define CC_POLL_MS 5U
#define CC_ATTACH_MS 100U
#define CC_INCREASE_MS 15U
#define CHARGER_RETRY_MS 100U

static bool monitor_ready;
static bool charger_ready;
static bool vbus_present;
static bool write_failed;
static uint32_t last_poll;
static uint32_t candidate_since;
static uint16_t candidate_ma;
static uint16_t source_ma;
static uint16_t input_ma = USB_CONFIGURED_CURRENT_MA;
static uint16_t measured_cc1_mv;
static uint16_t measured_cc2_mv;
static bool charging_enabled;

/* Use the valid vRd bands in ST AN5225, Table 7. A regular source has
 * one active CC pin. Values in the gaps or on both pins stay unqualified.
 */
static uint16_t advertised_current(uint16_t cc1_mv, uint16_t cc2_mv) {
  uint16_t active_mv = cc1_mv > cc2_mv ? cc1_mv : cc2_mv;
  uint16_t idle_mv = cc1_mv > cc2_mv ? cc2_mv : cc1_mv;
  if (idle_mv >= 200U)
    return 0U;
  if (active_mv >= 250U && active_mv <= 610U)
    return USB_CONFIGURED_CURRENT_MA;
  if (active_mv >= 700U && active_mv <= 1160U)
    return 1500U;
  if (active_mv >= 1310U && active_mv <= 2040U)
    return 3000U;
  return 0U;
}

static bool configure_charger(void) {
  bq25883_status_t status = bq25883_init();
  if (status != BQ25883_OK) {
    LOG_ERROR("BQ25883: %s", bq25883_status_name(status));
    return false;
  }
  LOG_INFO("BQ25883: configured with charging inhibited");
  return true;
}

void usb_charge_init(void) {
  board_i2c_batt_init();
  if (i2c_init(&batt_i2c_config) != I2C_STATUS_OK) {
    LOG_ERROR("BQ25883: I2C init failed");
    return;
  }
  charger_ready = configure_charger();
  monitor_ready = board_usb_cc_init();
  if (!monitor_ready)
    LOG_ERROR("USB-C: CC ADC init failed");
}

void usb_charge_task(void) {
  if (!monitor_ready)
    return;

  uint32_t now = time_ms();
  bool present = board_battery_power_good();
  uint32_t interval =
      charger_ready && !write_failed ? CC_POLL_MS : CHARGER_RETRY_MS;
  if (present == vbus_present && now - last_poll < interval)
    return;
  last_poll = now;

  if (!charger_ready) {
    if (!present)
      return;
    charger_ready = configure_charger();
    if (!charger_ready) {
      vbus_present = present;
      return;
    }
  }

  uint16_t cc1_mv = 0U;
  uint16_t cc2_mv = 0U;
  bool sampled = present && board_usb_cc_read_mv(&cc1_mv, &cc2_mv);
  measured_cc1_mv = sampled ? cc1_mv : 0U;
  measured_cc2_mv = sampled ? cc2_mv : 0U;
  uint16_t observed_ma = sampled ? advertised_current(cc1_mv, cc2_mv) : 0U;
  if (present && !vbus_present) {
    if (sampled)
      LOG_INFO("USB-C: CC1 %u mV, CC2 %u mV, advertised %u mA",
               (unsigned)cc1_mv, (unsigned)cc2_mv, (unsigned)observed_ma);
    else
      LOG_ERROR("USB-C: CC ADC read failed");
  }
  vbus_present = present;

  if (observed_ma != candidate_ma) {
    candidate_ma = observed_ma;
    candidate_since = now;
  }
  uint32_t qualify_ms = source_ma == 0U ? CC_ATTACH_MS : CC_INCREASE_MS;
  /* Reduce draw immediately. Require a stable CC reading before increasing it.
   */
  if (observed_ma < source_ma || now - candidate_since >= qualify_ms)
    source_ma = observed_ma;

  uint16_t limit = source_ma ? source_ma : USB_CONFIGURED_CURRENT_MA;
  bool enable = source_ma > USB_CONFIGURED_CURRENT_MA ||
                (source_ma == USB_CONFIGURED_CURRENT_MA &&
                 usb_configured_power_available());
  if (!write_failed && limit == input_ma && enable == charging_enabled)
    return;
  bq25883_status_t status = bq25883_apply_usb_current(limit, enable);
  if (status != BQ25883_OK) {
    if (!write_failed)
      LOG_ERROR("BQ25883: USB current update failed: %s",
                bq25883_status_name(status));
    write_failed = true;
    return;
  }
  write_failed = false;
  input_ma = limit;
  charging_enabled = enable;
  LOG_INFO("BQ25883: input limit %u mA, charging %s", (unsigned)input_ma,
           enable ? "enabled" : "inhibited");
}

usb_charge_status_t usb_charge_status(void) {
  return (usb_charge_status_t){.ready = charger_ready && monitor_ready,
                               .update_failed = write_failed,
                               .cc1_mv = measured_cc1_mv,
                               .cc2_mv = measured_cc2_mv,
                               .advertised_ma = source_ma,
                               .input_limit_ma = input_ma};
}
