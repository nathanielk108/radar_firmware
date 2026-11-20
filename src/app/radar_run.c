#include "app/radar_run.h"

#include "platform/time.h"
#include "platform/usb.h"
#include "services/chirp.h"
#include "services/recording.h"
#include "system/firmware.h"
#include "system/log.h"

#include <stdio.h>
#include <string.h>

#define USB_CHUNK_BYTES 64U
#define USB_TX_TIMEOUT_MS 5000U

typedef enum {
  RUN_OPEN_OUTPUT,
  RUN_WAIT,
  RUN_ACQUIRE,
  RUN_SAVE,
  RUN_USB_FINISH
} run_phase_t;

static radar_run_status_t status;
static radar_run_config_t settings;
static run_phase_t phase;
static chirp_capture_t capture;
static bool stop_requested;
static uint16_t gain_mv;
static uint32_t last_save_ms;
static uint32_t next_manifest;
static radar_run_state_t usb_final_state;
static uint32_t usb_last_progress_ms;
static uint32_t usb_header[RECORDING_HEADER_WORDS];
static size_t usb_offset;
static bool usb_capture_announced;

static bool valid_config(const radar_run_config_t *config) {
  if (!config || !config->captures || config->captures > RADAR_RUN_MAX_CAPTURES ||
      !config->chirps || config->chirps > CHIRP_MAX_COUNT ||
      config->gap_ms > RADAR_RUN_MAX_GAP_MS ||
      (unsigned)config->output > RADAR_RUN_OUTPUT_USB)
    return false;
  if (config->chirps == 1U ? config->interval_us != 0U
                          : config->interval_us < CHIRP_MIN_REPETITION_US ||
                                config->interval_us > CHIRP_MAX_REPETITION_US)
    return false;
  for (size_t i = 0; i < sizeof(config->tag); ++i) {
    char ch = config->tag[i];
    if (ch == '\0')
      return i != 0U;
    if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
          (ch >= '0' && ch <= '9') || ch == '_' || ch == '-'))
      return false;
  }
  return false;
}

radar_run_start_result_t radar_run_start(const radar_run_config_t *config) {
  if (status.state == RADAR_RUN_RUNNING || chirp_capture_active() ||
      acquisition_active())
    return RADAR_RUN_START_BUSY;
  if (!valid_config(config))
    return RADAR_RUN_START_INVALID;
  if (!firmware_gain_get(&gain_mv))
    return RADAR_RUN_START_RF_UNAVAILABLE;
  if (config->output == RADAR_RUN_OUTPUT_USB && !usb_console_connected())
    return RADAR_RUN_START_USB_UNAVAILABLE;
  if (config->output == RADAR_RUN_OUTPUT_SD && !storage_mounted())
    return RADAR_RUN_START_SD_UNAVAILABLE;

  settings = *config;
  status = (radar_run_status_t){
      .state = RADAR_RUN_RUNNING,
      .output = config->output,
      .captures = config->captures,
  };
  stop_requested = false;
  phase = RUN_OPEN_OUTPUT;
  usb_last_progress_ms = time_ms();
  return RADAR_RUN_START_OK;
}

bool radar_run_stop(void) {
  if (status.state != RADAR_RUN_RUNNING)
    return false;
  stop_requested = true;
  return true;
}

radar_run_status_t radar_run_status(void) { return status; }

const char *radar_run_state_name(radar_run_state_t state) {
  switch (state) {
  case RADAR_RUN_RUNNING:
    return "running";
  case RADAR_RUN_COMPLETE:
    return "complete";
  case RADAR_RUN_STOPPED:
    return "stopped";
  case RADAR_RUN_CAPTURE_ERROR:
    return "capture_error";
  case RADAR_RUN_STORAGE_ERROR:
    return "storage_error";
  case RADAR_RUN_USB_ERROR:
    return "usb_error";
  default:
    return "idle";
  }
}

static storage_status_t write_close(const char *text, int length, size_t capacity) {
  storage_status_t result = length >= 0 && (size_t)length < capacity
                                ? storage_write(text, (size_t)length)
                                : STORAGE_INVALID_ARGUMENT;
  storage_status_t close = storage_close();
  return result == STORAGE_OK ? close : result;
}

static storage_status_t append_manifest(const char *text, int length,
                                        size_t capacity) {
  storage_status_t result = storage_open(status.path, STORAGE_OPEN_APPEND);
  return result == STORAGE_OK ? write_close(text, length, capacity) : result;
}

static void report_finished(radar_run_state_t state) {
  status.state = state;
  if (settings.output == RADAR_RUN_OUTPUT_USB) {
    LOG_INFO("USB run %s %s %u/%u capture=%s", settings.tag,
             radar_run_state_name(state), (unsigned)status.saved,
             (unsigned)status.captures,
             acquisition_status_name(status.capture_error));
    return;
  }
  LOG_INFO("Run %s %s %u/%u capture=%s sd=%s",
           status.path[0] ? status.path : settings.tag, radar_run_state_name(state),
           (unsigned)status.saved, (unsigned)status.captures,
           acquisition_status_name(status.capture_error),
           storage_status_name(status.storage_error));
}

static void finish(radar_run_state_t state, bool write_footer) {
  if (settings.output == RADAR_RUN_OUTPUT_USB) {
    usb_final_state = state;
    usb_last_progress_ms = time_ms();
    phase = RUN_USB_FINISH;
    return;
  }
  if (write_footer && status.path[0] && storage_mounted()) {
    char footer[64];
    int length = snprintf(footer, sizeof(footer), "# status=%s,saved=%u\n",
                          radar_run_state_name(state), (unsigned)status.saved);
    storage_status_t result = append_manifest(footer, length, sizeof(footer));
    if (result != STORAGE_OK) {
      if (status.storage_error == STORAGE_OK)
        status.storage_error = result;
      state = RADAR_RUN_STORAGE_ERROR;
    }
  }
  report_finished(state);
}

static void create_manifest(void) {
  char path[sizeof(status.path)];
  (void)snprintf(path, sizeof(path), "R%07lu.CSV", (unsigned long)next_manifest);
  storage_status_t result = storage_open(path, STORAGE_OPEN_NEW);
  if (result == STORAGE_OK || result == STORAGE_EXISTS)
    next_manifest = (next_manifest + 1U) % 10000000U;
  if (result == STORAGE_EXISTS || result == STORAGE_BUSY)
    return;
  if (result == STORAGE_OK) {
    char header[192];
    int length = snprintf(header, sizeof(header),
                          "# radar_run=1,tag=%s,gain_mv=%u,captures=%u,chirps=%u,"
                          "interval_us=%lu,gap_ms=%lu\n"
                          "index,file,sequence,first_sample_us\n",
                          settings.tag, (unsigned)gain_mv,
                          (unsigned)settings.captures, (unsigned)settings.chirps,
                          (unsigned long)settings.interval_us,
                          (unsigned long)settings.gap_ms);
    result = write_close(header, length, sizeof(header));
  }
  if (result != STORAGE_OK) {
    status.storage_error = result;
    finish(RADAR_RUN_STORAGE_ERROR, false);
    return;
  }
  memcpy(status.path, path, sizeof(path));
  LOG_INFO("Run %s started, tag=%s", status.path, settings.tag);
  phase = RUN_WAIT;
}

static void arm_capture(void) {
  if (status.saved && !time_expired(last_save_ms, settings.gap_ms))
    return;
  acquisition_status_t result =
      settings.chirps == 1U
          ? chirp_capture_start()
          : chirp_capture_train_start(settings.chirps, settings.interval_us);
  if (result == ACQUISITION_OK) {
    phase = RUN_ACQUIRE;
  } else {
    status.capture_error = result;
    finish(RADAR_RUN_CAPTURE_ERROR, true);
  }
}

static void capture_saved(void) {
  ++status.saved;
  if (status.saved == status.captures) {
    finish(RADAR_RUN_COMPLETE, true);
  } else {
    last_save_ms = time_ms();
    phase = RUN_WAIT;
  }
}

static void save_capture(void) {
  char path[RECORDING_FILENAME_BYTES];
  storage_status_t result = recording_save_capture(&capture, path);
  if (result == STORAGE_BUSY)
    return;
  if (result != STORAGE_OK) {
    status.storage_error = result;
    finish(RADAR_RUN_STORAGE_ERROR, true);
    return;
  }
  char row[64];
  int length = snprintf(row, sizeof(row), "%u,%s,%lu,%lu\n",
                        (unsigned)status.saved + 1U, path,
                        (unsigned long)capture.samples.sequence,
                        (unsigned long)capture.samples.first_sample_us);
  result = append_manifest(row, length, sizeof(row));
  if (result != STORAGE_OK) {
    status.storage_error = result;
    /* A failed append can leave a partial row. Keep it at EOF for recovery. */
    LOG_ERROR("Run %s saved %s, manifest update failed: %s", status.path,
              path, storage_status_name(result));
    finish(RADAR_RUN_STORAGE_ERROR, false);
    return;
  }
  capture_saved();
}

static bool queue_usb_line(const char *text, int length, size_t capacity) {
  if (length < 0 || (size_t)length >= capacity) {
    report_finished(RADAR_RUN_USB_ERROR);
    return false;
  }
  if (!usb_console_write_bytes(text, (size_t)length))
    return false;
  usb_last_progress_ms = time_ms();
  return true;
}

static void open_usb_output(void) {
  char header[160];
  int length = snprintf(header, sizeof(header),
                        "# radar_usb=1,tag=%s,gain_mv=%u,captures=%u,chirps=%u,"
                        "interval_us=%lu,gap_ms=%lu\r\n",
                        settings.tag, (unsigned)gain_mv,
                        (unsigned)settings.captures, (unsigned)settings.chirps,
                        (unsigned long)settings.interval_us,
                        (unsigned long)settings.gap_ms);
  if (queue_usb_line(header, length, sizeof(header)))
    phase = RUN_WAIT;
}

static void finish_usb_output(void) {
  char footer[60];
  int length = snprintf(footer, sizeof(footer), "# status=%s,saved=%u\r\n",
                        radar_run_state_name(usb_final_state),
                        (unsigned)status.saved);
  if (queue_usb_line(footer, length, sizeof(footer)))
    report_finished(usb_final_state);
}

static void send_usb_capture(void) {
  size_t channel_bytes = capture.samples.count * sizeof(int16_t);
  size_t total = sizeof(usb_header) + 2U * capture.chirp_count * channel_bytes;
  if (!usb_capture_announced) {
    char metadata[80];
    int length = snprintf(metadata, sizeof(metadata),
                          "# capture=%u,first_sample_us=%lu,bytes=%lu\r\n",
                          (unsigned)status.saved + 1U,
                          (unsigned long)capture.samples.first_sample_us,
                          (unsigned long)total);
    usb_capture_announced = queue_usb_line(metadata, length, sizeof(metadata));
    return;
  }

  /* RDR1 stores A then B for each chirp. Skip stride padding in the DMA view. */
  const uint8_t *source;
  size_t remaining;
  if (usb_offset < sizeof(usb_header)) {
    source = (const uint8_t *)usb_header + usb_offset;
    remaining = sizeof(usb_header) - usb_offset;
  } else {
    size_t offset = usb_offset - sizeof(usb_header);
    size_t channel = offset / channel_bytes;
    size_t in_channel = offset % channel_bytes;
    const int16_t *codes = (channel % 2U) ? capture.samples.b : capture.samples.a;
    source = (const uint8_t *)(codes + channel / 2U * capture.sample_stride) +
             in_channel;
    remaining = channel_bytes - in_channel;
  }
  size_t bytes = remaining > USB_CHUNK_BYTES ? USB_CHUNK_BYTES : remaining;
  static const char hex[] = "0123456789abcdef";
  char line[sizeof("# data=\r\n") + 2U * USB_CHUNK_BYTES] = "# data=";
  size_t length = sizeof("# data=") - 1U;
  for (size_t i = 0; i < bytes; ++i) {
    line[length++] = hex[source[i] >> 4];
    line[length++] = hex[source[i] & 0x0FU];
  }
  line[length++] = '\r';
  line[length++] = '\n';
  if (queue_usb_line(line, (int)length, sizeof(line))) {
    usb_offset += bytes;
    if (usb_offset == total)
      capture_saved();
  }
}

void radar_run_poll(void) {
  if (status.state != RADAR_RUN_RUNNING)
    return;
  bool usb = settings.output == RADAR_RUN_OUTPUT_USB;
  bool sending = phase == RUN_OPEN_OUTPUT || phase == RUN_SAVE ||
                 phase == RUN_USB_FINISH;
  if (usb && (!usb_console_connected() ||
              (sending && time_expired(usb_last_progress_ms, USB_TX_TIMEOUT_MS)))) {
    if (phase == RUN_ACQUIRE)
      status.capture_error = chirp_capture_cancel();
    report_finished(RADAR_RUN_USB_ERROR);
    return;
  }
  if (phase == RUN_USB_FINISH) {
    finish_usb_output();
    return;
  }
  /* Always put the USB run header before any terminal status. */
  if (usb && phase == RUN_OPEN_OUTPUT) {
    open_usb_output();
    return;
  }
  bool mounted = usb || storage_mounted();
  if (stop_requested || !mounted) {
    if (phase == RUN_ACQUIRE)
      status.capture_error = chirp_capture_cancel();
    if (!mounted) {
      status.storage_error = storage_present() ? STORAGE_NOT_MOUNTED : STORAGE_NO_MEDIA;
      finish(RADAR_RUN_STORAGE_ERROR, false);
    } else {
      finish(status.capture_error == ACQUISITION_OK ? RADAR_RUN_STOPPED
                                                   : RADAR_RUN_CAPTURE_ERROR, true);
    }
    return;
  }
  switch (phase) {
  case RUN_OPEN_OUTPUT:
    create_manifest();
    break;
  case RUN_WAIT:
    arm_capture();
    break;
  case RUN_ACQUIRE: {
    acquisition_status_t result = chirp_capture_poll(&capture);
    if (result == ACQUISITION_OK) {
      if (usb) {
        recording_encode_header(&capture, usb_header);
        usb_offset = 0U;
        usb_capture_announced = false;
        usb_last_progress_ms = time_ms();
      }
      phase = RUN_SAVE;
    } else if (result != ACQUISITION_BUSY) {
      status.capture_error = result;
      finish(RADAR_RUN_CAPTURE_ERROR, true);
    }
    break;
  }
  case RUN_SAVE:
    if (usb)
      send_usb_capture();
    else
      save_capture();
    break;
  case RUN_USB_FINISH:
    break;
  }
}
