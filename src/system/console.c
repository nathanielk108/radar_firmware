#include "system/console.h"

#include "dsp/transform_functions.h"
#include "platform/fault.h"
#include "platform/time.h"
#include "platform/usb.h"
#include "services/acquisition.h"
#include "services/chirp.h"
#include "services/display.h"
#include "services/motion.h"
#include "services/recording.h"
#include "services/storage.h"
#include "services/usb_charge.h"
#include "system/firmware.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONSOLE_LINE_BYTES 80U
#define STREAM_TEST_MS 1000U

static char line[CONSOLE_LINE_BYTES];
static size_t line_length;
static bool line_overflow;
static bool waiting_for_capture;
static bool print_csv;
static uint16_t capture_gain_mv;
static char csv[1536];
static size_t csv_size;
static uint32_t reported_saved;

static void reply(const char *format, ...) {
  char text[384];
  va_list args;
  va_start(args, format);
  int length = vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  if (length < 0 || (size_t)length >= sizeof(text) - 2U)
    return;
  text[length++] = '\r';
  text[length++] = '\n';
  recording_note(text, (size_t)length);
  (void)usb_console_write_bytes(text, (size_t)length);
}

static bool capture_active(void) {
  return waiting_for_capture || chirp_capture_active() || acquisition_active();
}

static void format_csv(const chirp_capture_t *capture) {
  int length = snprintf(csv, sizeof(csv),
                        "# sample_rate_hz=%lu,step_hz=%lu,step_rate_hz=%lu,"
                        "samples=%u,gain_mv=%u\r\nindex,adc_a,adc_b\r\n",
                        (unsigned long)capture->sample_rate_hz,
                        (unsigned long)capture->step_hz,
                        (unsigned long)capture->step_rate_hz,
                        (unsigned)capture->samples.count,
                        (unsigned)capture_gain_mv);
  if (length < 0 || (size_t)length >= sizeof(csv))
    return;
  size_t used = (size_t)length;
  for (uint16_t i = 0; i < capture->samples.count; ++i) {
    length = snprintf(csv + used, sizeof(csv) - used, "%u,%d,%d\r\n",
                      (unsigned)i, (int)capture->samples.a[i],
                      (int)capture->samples.b[i]);
    if (length < 0 || (size_t)length >= sizeof(csv) - used)
      return;
    used += (size_t)length;
  }
  csv_size = used;
}

static void start_capture(bool usb_csv, uint16_t count, uint32_t interval_us) {
  if (capture_active() || csv_size) {
    reply("RADAR busy");
    return;
  }
  if (!firmware_gain_get(&capture_gain_mv)) {
    reply("RADAR unavailable; attach RF board and use rf on");
    return;
  }
  if (!usb_csv && !storage_mounted()) {
    reply("Mount an SD card with sd mount before saving a capture");
    return;
  }
  acquisition_status_t result = count == 1U
                                    ? chirp_capture_start()
                                    : chirp_capture_train_start(count, interval_us);
  if (result != ACQUISITION_OK) {
    reply("RADAR start: %s", acquisition_status_name(result));
    return;
  }
  print_csv = usb_csv;
  waiting_for_capture = true;
}

static void finish_capture(void) {
  if (!waiting_for_capture)
    return;
  chirp_capture_t capture;
  acquisition_status_t result = chirp_capture_poll(&capture);
  if (result == ACQUISITION_BUSY)
    return;
  waiting_for_capture = false;
  if (result != ACQUISITION_OK) {
    reply("RADAR capture: %s", acquisition_status_name(result));
    return;
  }
  if (print_csv && usb_console_connected())
    format_csv(&capture);
  if ((!print_csv || storage_mounted()) &&
      !recording_capture(&capture, capture_gain_mv))
    reply("RADAR capture complete; SD queue full");
}

static void status_command(void) {
  firmware_status_t system = firmware_status();
  recording_status_t log = recording_status();
  reply("uptime=%lu ms usb=%s vbus=%u charging_allowed=%u efuse_pg=%u "
        "imu1=%u imu2=%u radar=%u lcd=%u sd_present=%u sd_mounted=%u",
        (unsigned long)time_ms(), system.usb_ready ? "ready" : "unavailable",
        (unsigned)system.vbus_present, (unsigned)!system.charge_inhibited,
        (unsigned)system.efuse_power_good, (unsigned)motion_ready(MOTION_ISM330IS),
        (unsigned)motion_ready(MOTION_BMI323), (unsigned)system.radar_ready,
        (unsigned)display_enabled(), (unsigned)storage_present(),
        (unsigned)storage_mounted());
  reply("sd_saved=%lu pending=%u dropped=%lu log_dropped=%lu io_errors=%lu "
        "last_error=%s last_file=%s",
        (unsigned long)log.captures_saved, (unsigned)log.captures_pending,
        (unsigned long)log.captures_dropped,
        (unsigned long)log.diagnostic_lines_dropped,
        (unsigned long)log.write_errors, storage_status_name(log.last_error),
        log.last_capture[0] ? log.last_capture : "-");
}

static void power_command(void) {
  firmware_status_t system = firmware_status();
  usb_charge_status_t charger = usb_charge_status();
  reply("vbus=%u efuse_pg=%u charger_ready=%u update_failed=%u "
        "cc1_mv=%u cc2_mv=%u advertised_ma=%u input_limit_ma=%u inhibited=%u",
        (unsigned)system.vbus_present, (unsigned)system.efuse_power_good,
        (unsigned)charger.ready, (unsigned)charger.update_failed,
        (unsigned)charger.cc1_mv, (unsigned)charger.cc2_mv,
        (unsigned)charger.advertised_ma, (unsigned)charger.input_limit_ma,
        (unsigned)system.charge_inhibited);
}

static void motion_command(motion_sensor_t sensor, const char *name) {
  uint16_t id = 0U;
  motion_status_t result = motion_read_id(sensor, &id);
  if (result != MOTION_OK) {
    reply("%s: %s", name, motion_status_name(result));
    return;
  }
  motion_sample_t sample;
  result = motion_read(sensor, &sample);
  if (result != MOTION_OK) {
    reply("%s id=0x%02X: %s", name, (unsigned)id, motion_status_name(result));
    return;
  }
  reply("%s id=0x%02X t=%lu ms accel_ug=%ld,%ld,%ld gyro_mdps=%ld,%ld,%ld",
        name, (unsigned)id, (unsigned long)sample.timestamp_ms,
        (long)sample.accel_ug[0], (long)sample.accel_ug[1],
        (long)sample.accel_ug[2], (long)sample.gyro_mdps[0],
        (long)sample.gyro_mdps[1], (long)sample.gyro_mdps[2]);
}

static void adc_test(void) {
  if (!firmware_status().radar_ready) {
    reply("ADC unavailable; attach RF board and use rf on");
    return;
  }
  acquisition_block_t block;
  acquisition_status_t result = acquisition_capture_block(64U, &block);
  if (result != ACQUISITION_OK) {
    reply("ADC test: %s", acquisition_status_name(result));
    return;
  }
  int16_t a_min = block.a[0], a_max = a_min;
  int16_t b_min = block.b[0], b_max = b_min;
  for (uint16_t i = 1; i < block.count; ++i) {
    if (block.a[i] < a_min)
      a_min = block.a[i];
    if (block.a[i] > a_max)
      a_max = block.a[i];
    if (block.b[i] < b_min)
      b_min = block.b[i];
    if (block.b[i] > b_max)
      b_max = block.b[i];
  }
  reply("ADC DMA %u samples, A=[%d,%d] B=[%d,%d]", (unsigned)block.count,
        (int)a_min, (int)a_max, (int)b_min, (int)b_max);
}

static void stream_test(void) {
  if (!firmware_status().radar_ready) {
    reply("ADC unavailable; attach RF board and use rf on");
    return;
  }
  const acquisition_timed_config_t config = {
      .sample_rate_hz = ACQUISITION_SAMPLE_RATE_HZ,
      .samples_per_block = ACQUISITION_MAX_BLOCK_SAMPLES,
  };
  acquisition_status_t result = acquisition_timed_init(&config);
  if (result != ACQUISITION_OK) {
    reply("ADC stream init: %s", acquisition_status_name(result));
    return;
  }
  result = acquisition_timed_start();
  uint32_t blocks = 0U;
  uint32_t started = time_ms();
  while (result == ACQUISITION_OK && !time_expired(started, STREAM_TEST_MS)) {
    acquisition_block_t block;
    result = acquisition_timed_get_block(&block);
    if (result == ACQUISITION_BUSY)
      result = ACQUISITION_OK;
    else if (result == ACQUISITION_OK) {
      ++blocks;
      result = acquisition_timed_release_block(block.sequence);
    }
    firmware_poll();
  }
  acquisition_status_t stop = acquisition_timed_stop();
  acquisition_status_t cleanup = acquisition_timed_deinit();
  if (result == ACQUISITION_OK)
    result = stop == ACQUISITION_OK ? cleanup : stop;
  if (result == ACQUISITION_OK && blocks == 0U)
    result = ACQUISITION_TIMEOUT;
  reply("ADC stream %lu blocks in %lu ms: %s", (unsigned long)blocks,
        (unsigned long)time_elapsed_ms(started), acquisition_status_name(result));
}

static void dsp_test(void) {
  static const float tone[] = {0.0f, 0.70710678f, 1.0f, 0.70710678f,
                               0.0f, -0.70710678f, -1.0f, -0.70710678f};
  float input[64], bins[64];
  arm_rfft_fast_instance_f32 fft;
  if (arm_rfft_fast_init_64_f32(&fft) != ARM_MATH_SUCCESS) {
    reply("DSP FFT init failed");
    return;
  }
  for (unsigned i = 0; i < 64U; ++i)
    input[i] = tone[i % 8U];
  arm_rfft_fast_f32(&fft, input, bins, 0U);
  bool pass = bins[0] == 0.0f && bins[1] == 0.0f;
  for (unsigned bin = 1U; bin < 32U; ++bin) {
    float power = bins[2U * bin] * bins[2U * bin] +
                  bins[2U * bin + 1U] * bins[2U * bin + 1U];
    pass = pass && (bin == 8U ? power > 1023.0f && power < 1025.0f
                               : power < 0.001f);
  }
  reply("DSP 64-point real FFT, tone bin 8: %s", pass ? "PASS" : "FAIL");
}

static void sd_test(void) {
  static uint8_t expected[1024], actual[1024];
  char name[13];
  storage_status_t result = STORAGE_EXISTS;
  for (unsigned id = 0; id < 100U && result == STORAGE_EXISTS; ++id) {
    (void)snprintf(name, sizeof(name), "SDTEST%02u.DAT", id);
    result = storage_open(name, STORAGE_OPEN_NEW);
  }
  if (result == STORAGE_OK) {
    for (size_t i = 0; i < sizeof(expected); ++i)
      expected[i] = (uint8_t)(73U * i + (i >> 8));
    result = storage_write(expected, sizeof(expected));
    storage_status_t close = storage_close();
    if (result == STORAGE_OK)
      result = close;
  }
  if (result == STORAGE_OK)
    result = storage_open(name, STORAGE_OPEN_READ);
  size_t count = 0U;
  if (result == STORAGE_OK) {
    result = storage_read(actual, sizeof(actual), &count);
    storage_status_t close = storage_close();
    if (result == STORAGE_OK)
      result = close;
  }
  if (result != STORAGE_OK)
    reply("SD test: %s", storage_status_name(result));
  else
    reply("SD test %s: %s (%u bytes)", name,
          count == sizeof(actual) && !memcmp(expected, actual, sizeof(actual))
              ? "PASS"
              : "FAIL", (unsigned)count);
}

static bool parse_unsigned(const char *value, uint32_t max, uint32_t *result) {
  if (!value || value[0] < '0' || value[0] > '9')
    return false;
  char *end;
  unsigned long number = strtoul(value, &end, 10);
  if (*end || number > max)
    return false;
  *result = (uint32_t)number;
  return true;
}

static void dispatch(char *command) {
  if (!strncmp(command, "echo ", 5U)) {
    reply("%s", command + 5U);
    return;
  }
  char *name = strtok(command, " \t");
  if (!name)
    return;
  char *arg = strtok(NULL, " \t");
  char *extra = strtok(NULL, " \t");
  if (!strcmp(name, "help") && !arg) {
    reply("status | power | imu [1|2] | rf on|off | g [mV] | c | "
          "save [chirps [interval_us]] | sd mount|eject|test | "
          "test adc|stream|dsp|lcd | lcd off | echo <text>");
    return;
  }
  if (!strcmp(name, "status") && !arg) {
    status_command();
    return;
  }
  if (!strcmp(name, "power") && !arg) {
    power_command();
    return;
  }
  if (capture_active()) {
    reply("RADAR busy");
    return;
  }
  if (!strcmp(name, "g") && !extra) {
    uint32_t value;
    uint16_t millivolts;
    if (!firmware_gain_get(&millivolts))
      reply("GAIN_DAC unavailable; RF is off");
    else if (arg && (!parse_unsigned(arg, FIRMWARE_GAIN_MAX_MV, &value) ||
                     !firmware_gain_set((uint16_t)value)))
      reply("GAIN_DAC range %u-%u mV", FIRMWARE_GAIN_MIN_MV, FIRMWARE_GAIN_MAX_MV);
    else if (firmware_gain_get(&millivolts))
      reply("GAIN_DAC %u mV (nominal)", (unsigned)millivolts);
  } else if (!strcmp(name, "save") && !strtok(NULL, " \t")) {
    uint32_t count = 1U, interval = 1000U;
    if ((arg && (!parse_unsigned(arg, CHIRP_MAX_COUNT, &count) || count == 0U)) ||
        (extra && (count < 2U ||
                   !parse_unsigned(extra, CHIRP_MAX_REPETITION_US, &interval) ||
                   interval < CHIRP_MIN_REPETITION_US)))
      reply("save [1-%u chirps [%u-%u us for trains]]", CHIRP_MAX_COUNT,
            CHIRP_MIN_REPETITION_US, CHIRP_MAX_REPETITION_US);
    else
      start_capture(false, (uint16_t)count, interval);
  } else if (!strcmp(name, "rf") && arg && !extra &&
             (!strcmp(arg, "on") || !strcmp(arg, "off"))) {
    if (firmware_rf_enable(!strcmp(arg, "on")))
      reply("RF %s", arg);
    else
      reply("RF unavailable; check ADC and RF rail connections");
  } else if (!strcmp(name, "imu") && !extra &&
             (!arg || !strcmp(arg, "1") || !strcmp(arg, "2"))) {
    if (!arg || !strcmp(arg, "1"))
      motion_command(MOTION_ISM330IS, "ISM330IS");
    if (!arg || !strcmp(arg, "2"))
      motion_command(MOTION_BMI323, "BMI323");
  } else if (!strcmp(name, "sd") && arg && !extra) {
    if (!strcmp(arg, "mount"))
      reply("SD mount: %s", storage_status_name(storage_mount()));
    else if (!strcmp(arg, "eject")) {
      storage_status_t result = recording_flush();
      if (result == STORAGE_OK)
        result = storage_unmount();
      reply("SD eject: %s", storage_status_name(result));
    } else if (!strcmp(arg, "test"))
      sd_test();
    else
      reply("sd mount|eject|test");
  } else if (!strcmp(name, "test") && arg && !extra) {
    if (!strcmp(arg, "adc"))
      adc_test();
    else if (!strcmp(arg, "stream"))
      stream_test();
    else if (!strcmp(arg, "dsp"))
      dsp_test();
    else if (!strcmp(arg, "lcd")) {
      display_status_t result = display_enable(true);
      if (result == DISPLAY_OK)
        result = display_test_pattern();
      reply("LCD pattern: %s", display_status_name(result));
    }
#if defined(FIRMWARE_DEBUG)
    else if (!strcmp(arg, "fault")) {
      (void)firmware_rf_enable(false);
      (void)display_enable(false);
      fault_test_usage();
    }
#endif
    else
      reply("test adc|stream|dsp|lcd");
  } else if (!strcmp(name, "lcd") && arg && !strcmp(arg, "off") && !extra) {
    reply("LCD off: %s", display_status_name(display_enable(false)));
  } else {
    reply("Unknown command; send help");
  }
}

void console_poll(void) {
  finish_capture();
  if (!usb_console_connected()) {
    line_length = csv_size = 0U;
    line_overflow = false;
    return;
  }
  if (csv_size && usb_console_write_bytes(csv, csv_size))
    csv_size = 0U;
  recording_status_t log = recording_status();
  if (log.captures_saved != reported_saved) {
    reported_saved = log.captures_saved;
    reply("Saved %s", log.last_capture);
  }

  char input[32];
  uint32_t count = usb_console_read(input, sizeof(input));
  for (uint32_t i = 0U; i < count; ++i) {
    char ch = input[i];
    if (ch == '\r' || ch == '\n') {
      if (line_overflow)
        reply("Command too long");
      else if (line_length) {
        line[line_length] = '\0';
        dispatch(line);
      }
      line_length = 0U;
      line_overflow = false;
    } else if (ch == 'c' && line_length == 0U && !line_overflow) {
      start_capture(true, 1U, 0U);
    } else if (line_length + 1U < sizeof(line) && !line_overflow) {
      line[line_length++] = ch;
    } else {
      line_overflow = true;
    }
  }
}
