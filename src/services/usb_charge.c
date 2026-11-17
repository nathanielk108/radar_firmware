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
#define BATTERY_PROBE_SETTLE_MS 25U
#define BATTERY_PROBE_INTERVAL_MS 1000U
#define BATTERY_ADC_INTERVAL_MS 125U
#define BATTERY_ADC_POLL_MS 25U
#define BATTERY_ADC_FRESH_MS 500U
#define BATTERY_MIN_MV 4500U
#define BATTERY_MAX_MV 8500U
#define THERMISTOR_MIN_PERMILLE 20
#define THERMISTOR_OPEN_PERMILLE 820

typedef enum {
  BATTERY_IDLE,
  BATTERY_SETTLING,
  BATTERY_READING_PROBE,
  BATTERY_READING_STATUS
} battery_phase_t;

static bool bus_ready;
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
static usb_battery_presence_t battery_presence;
static battery_phase_t battery_phase;
static bool load_enabled;
static bool battery_read_failed;
static bool charge_completed;
static bool safety_timer_expired;
static uint32_t last_probe;
static uint32_t battery_phase_started;
static uint32_t snapshot_time;
static bool snapshot_valid;
static bq25883_snapshot_t snapshot;

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
  bus_ready = i2c_init(&batt_i2c_config) == I2C_STATUS_OK;
  if (!bus_ready) {
    LOG_ERROR("BQ25883: I2C init failed");
    return;
  }
  charger_ready = configure_charger();
  monitor_ready = board_usb_cc_init();
  if (!monitor_ready)
    LOG_ERROR("USB-C: CC ADC init failed");
}

static bool voltage_plausible(uint16_t millivolts) {
  return millivolts >= BATTERY_MIN_MV && millivolts <= BATTERY_MAX_MV;
}

static bool thermistor_connected(void) {
  /* The board's 5.23 kΩ / 30.1 kΩ divider reads about 852‰ with J4 open. */
  return snapshot.ts_permille > THERMISTOR_MIN_PERMILLE &&
         snapshot.ts_permille < THERMISTOR_OPEN_PERMILLE;
}

static bq25883_status_t clear_battery_load(void) {
  if (!load_enabled)
    return BQ25883_OK;
  bq25883_status_t status = bq25883_set_battery_discharge(false);
  if (status == BQ25883_OK)
    load_enabled = false;
  return status;
}

static void recover_battery_bus(bq25883_status_t status) {
  if (status == BQ25883_ERROR_TIMEOUT || status == BQ25883_ERROR_BUS ||
      status == BQ25883_ERROR_ARBITRATION || status == BQ25883_ERROR_OVERRUN)
    board_i2c_batt_reset();
}

static void battery_read_error(bq25883_status_t status, uint32_t now) {
  if (!battery_read_failed)
    LOG_ERROR("BQ25883: battery check failed: %s", bq25883_status_name(status));
  battery_read_failed = true;
  battery_presence = USB_BATTERY_UNKNOWN;
  snapshot_valid = false;
  battery_phase = BATTERY_IDLE;
  battery_phase_started = now;
  board_battery_charge_inhibit(true);
  recover_battery_bus(status);
  recover_battery_bus(clear_battery_load());
}

/* True only in the iteration with a fresh, successful loaded measurement.
 * Passive telemetry can invalidate presence, but cannot grant permission. */
static bool sample_battery(uint32_t now, bool request_charge) {
  if (battery_phase == BATTERY_IDLE) {
    if (load_enabled) {
      if (now - battery_phase_started >= CHARGER_RETRY_MS) {
        battery_phase_started = now;
        recover_battery_bus(clear_battery_load());
      }
      return false;
    }
    bool needs_probe = !charging_enabled &&
                       (battery_presence != USB_BATTERY_PRESENT ||
                        (request_charge && !charge_completed &&
                         !safety_timer_expired && thermistor_connected()));
    if (needs_probe && now - last_probe >= BATTERY_PROBE_INTERVAL_MS) {
      last_probe = now;
      /* With CE high, BATFET isolates BAT from SYS. The 8 mA minimum
       * discharge current removes the charge on the board's 10 µF C31
       * before the ADC samples BAT. A connected pack sustains the load. */
      board_battery_charge_inhibit(true);
      load_enabled = true;
      bq25883_status_t status = bq25883_set_battery_discharge(true);
      if (status != BQ25883_OK) {
        battery_read_error(status, now);
        return false;
      }
      battery_phase = BATTERY_SETTLING;
      battery_phase_started = time_ms();
      return false;
    }
    if (now - battery_phase_started < BATTERY_ADC_INTERVAL_MS)
      return false;
  }

  if (battery_phase == BATTERY_IDLE || battery_phase == BATTERY_SETTLING) {
    bool probe = battery_phase == BATTERY_SETTLING;
    if (probe && now - battery_phase_started < BATTERY_PROBE_SETTLE_MS)
      return false;
    bq25883_status_t status = bq25883_adc_start();
    if (status != BQ25883_OK) {
      battery_read_error(status, now);
      return false;
    }
    battery_phase = probe ? BATTERY_READING_PROBE : BATTERY_READING_STATUS;
    battery_phase_started = now;
    return false;
  }

  if (now - battery_phase_started < BATTERY_ADC_POLL_MS)
    return false;
  if (now - battery_phase_started > BATTERY_ADC_FRESH_MS) {
    battery_read_error(BQ25883_ERROR_ADC_TIMEOUT, now);
    return false;
  }
  bq25883_snapshot_t sample;
  bool complete;
  bq25883_status_t status = bq25883_adc_poll(&sample, &complete);
  if (status != BQ25883_OK) {
    battery_read_error(status, now);
    return false;
  }
  if (!complete)
    return false;

  bool probe = battery_phase == BATTERY_READING_PROBE;
  status = clear_battery_load();
  if (status != BQ25883_OK) {
    battery_read_error(status, now);
    return false;
  }
  snapshot_time = battery_phase_started;
  battery_phase = BATTERY_IDLE;
  battery_phase_started = now;
  battery_read_failed = false;
  snapshot = sample;
  snapshot_valid = true;

  /* Toggling CE restarts the charger's safety timer. Latch expiry until
   * USB removal and avoid interrupting a healthy charge to probe BAT. */
  safety_timer_expired |= sample.safety_timer_expired;
  if (probe) {
    if (!sample.power_good || sample.vbat_mv > BATTERY_MAX_MV) {
      battery_presence = USB_BATTERY_UNKNOWN;
      return false;
    }
    if (sample.vbat_mv < BATTERY_MIN_MV) {
      battery_presence = USB_BATTERY_ABSENT;
      charge_completed = false;
      return false;
    }
    battery_presence = USB_BATTERY_PRESENT;
    if (sample.vbat_mv >= BQ25883_BOARD_CHARGE_VOLTAGE_MV)
      charge_completed = true;
    return true;
  }

  if (!sample.power_good || !voltage_plausible(sample.vbat_mv) ||
      (charging_enabled && !thermistor_connected())) {
    if (battery_presence == USB_BATTERY_PRESENT) {
      battery_presence = USB_BATTERY_UNKNOWN;
      last_probe = now - BATTERY_PROBE_INTERVAL_MS;
    }
  } else if (charging_enabled && sample.charge_state == BQ25883_CHARGE_DONE) {
    /* A bare BAT capacitor can also reach termination. Re-probe at the
     * end of charge and observe the normal pack recharge threshold. */
    charge_completed = true;
    battery_presence = USB_BATTERY_UNKNOWN;
    last_probe = now - BATTERY_PROBE_INTERVAL_MS;
  } else if (charge_completed && sample.vbat_mv <= BQ25883_BOARD_RECHARGE_MV) {
    charge_completed = false;
    last_probe = now - BATTERY_PROBE_INTERVAL_MS;
  }
  return false;
}

void usb_charge_task(void) {
  if (!bus_ready)
    return;

  uint32_t now = time_ms();
  bool present = board_battery_power_good();
  bool vbus_changed = present != vbus_present;
  uint32_t interval =
      charger_ready && !write_failed ? CC_POLL_MS : CHARGER_RETRY_MS;
  if (!vbus_changed && now - last_poll < interval)
    return;
  last_poll = now;

  if (vbus_changed) {
    if (!present)
      board_battery_charge_inhibit(true);
    battery_presence = USB_BATTERY_UNKNOWN;
    battery_phase = BATTERY_IDLE;
    last_probe = now - BATTERY_PROBE_INTERVAL_MS;
    snapshot_valid = false;
    charge_completed = false;
  }

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
  bool sampled = monitor_ready && (present || safety_timer_expired) &&
                 board_usb_cc_read_mv(&cc1_mv, &cc2_mv);
  measured_cc1_mv = present && sampled ? cc1_mv : 0U;
  measured_cc2_mv = present && sampled ? cc2_mv : 0U;
  uint16_t observed_ma = present && sampled ? advertised_current(cc1_mv, cc2_mv)
                                            : 0U;
  if (present && !vbus_present) {
    if (sampled)
      LOG_INFO("USB-C: CC1 %u mV, CC2 %u mV, advertised %u mA",
               (unsigned)cc1_mv, (unsigned)cc2_mv, (unsigned)observed_ma);
    else
      LOG_ERROR("USB-C: CC ADC read failed");
  }
  if (!present && sampled && cc1_mv < 200U && cc2_mv < 200U) {
    /* A PG drop can also indicate a USB supply fault with CC still attached. */
    safety_timer_expired = false;
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
  bool request_charge = source_ma > USB_CONFIGURED_CURRENT_MA ||
                        (source_ma == USB_CONFIGURED_CURRENT_MA &&
                         usb_configured_power_available());
  if (charging_enabled && !request_charge)
    board_battery_charge_inhibit(true);
  bool probed = false;
  if (present)
    probed = sample_battery(now, request_charge);
  else if (vbus_changed || load_enabled) {
    board_battery_charge_inhibit(true);
    (void)clear_battery_load();
  }

  bool enable = present && request_charge &&
                battery_presence == USB_BATTERY_PRESENT &&
                (charging_enabled || probed) && thermistor_connected() &&
                !charge_completed && !safety_timer_expired && snapshot_valid &&
                now - snapshot_time <= BATTERY_ADC_FRESH_MS;
  if (!write_failed && limit == input_ma && enable == charging_enabled)
    return;
  /* A source-current update can leave a healthy charge cycle running. */
  bq25883_status_t status = enable && charging_enabled
                           ? bq25883_set_input_current_limit_ma(limit)
                           : bq25883_apply_usb_current(limit, enable);
  if (status != BQ25883_OK) {
    board_battery_charge_inhibit(true);
    if (!write_failed)
      LOG_ERROR("BQ25883: USB current update failed: %s",
                bq25883_status_name(status));
    charging_enabled = false;
    battery_presence = USB_BATTERY_UNKNOWN;
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
                               .input_limit_ma = input_ma,
                               .battery_presence = battery_presence};
}

bool usb_charge_get_snapshot(bq25883_snapshot_t *out, uint32_t *age_ms) {
  uint32_t age = time_ms() - snapshot_time;
  if (!out || !snapshot_valid || age > BATTERY_ADC_FRESH_MS ||
      !board_battery_power_good())
    return false;
  *out = snapshot;
  if (age_ms)
    *age_ms = age;
  return true;
}
