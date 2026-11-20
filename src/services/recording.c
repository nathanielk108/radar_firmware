#include "services/recording.h"

#include "platform/time.h"

#include <stdio.h>
#include <string.h>

#define RECORD_HEADER_BYTES (RECORDING_HEADER_WORDS * sizeof(uint32_t))
#define RECORD_CAPTURE_QUEUE 4U
#define RECORD_DIAGNOSTIC_BYTES 8192U
#define RECORD_DIAGNOSTIC_INTERVAL_MS 250U

typedef struct {
  uint32_t header[RECORDING_HEADER_WORDS];
  int16_t codes[2U * ACQUISITION_MAX_BLOCK_SAMPLES * CHIRP_MAX_COUNT];
} capture_record_t;

_Static_assert(offsetof(capture_record_t, codes) == RECORD_HEADER_BYTES,
               "RDR1 samples must immediately follow the header");

static capture_record_t capture_queue[RECORD_CAPTURE_QUEUE];
static recording_status_t status;
static size_t capture_head;
static uint32_t next_file;
static char diagnostic[RECORD_DIAGNOSTIC_BYTES];
static size_t diagnostic_size;
static uint32_t diagnostic_lines;
static uint32_t last_diagnostic_write;

static bool valid_capture(const chirp_capture_t *capture) {
  return capture && capture->samples.a && capture->samples.b &&
         capture->samples.count &&
         capture->samples.count <= ACQUISITION_MAX_BLOCK_SAMPLES &&
         capture->sample_stride >= capture->samples.count &&
         capture->chirp_count && capture->chirp_count <= CHIRP_MAX_COUNT;
}

static capture_record_t *free_capture_slot(void) {
  if (status.captures_pending == RECORD_CAPTURE_QUEUE)
    return NULL;
  size_t tail = (capture_head + status.captures_pending) % RECORD_CAPTURE_QUEUE;
  return &capture_queue[tail];
}

void recording_encode_header(const chirp_capture_t *capture,
                             uint32_t header[RECORDING_HEADER_WORDS]) {
  uint16_t count = capture->samples.count;
  const uint32_t words[RECORDING_HEADER_WORDS] = {
      0x31524452U, /* RDR1, little-endian. */
      RECORD_HEADER_BYTES | ((uint32_t)count << 16),
      capture->samples.sequence,
      capture->timer_hz,
      (uint32_t)capture->first_sample_offset_ticks,
      capture->sample_rate_hz,
      capture->step_hz,
      capture->step_rate_hz,
      capture->bandwidth_hz,
      (uint32_t)capture->start_hz,
      (uint32_t)(capture->start_hz >> 32),
      capture->gain_mv,
      capture->repetition_ticks,
      capture->chirp_count,
  };
  memcpy(header, words, sizeof(words));
}

static void pack_capture(capture_record_t *record,
                         const chirp_capture_t *capture) {
  uint16_t count = capture->samples.count;
  recording_encode_header(capture, record->header);
  for (uint16_t chirp = 0; chirp < capture->chirp_count; ++chirp) {
    size_t source = (size_t)chirp * capture->sample_stride;
    int16_t *target = record->codes + (size_t)chirp * 2U * count;
    memcpy(target, capture->samples.a + source, count * sizeof(int16_t));
    memcpy(target + count, capture->samples.b + source, count * sizeof(int16_t));
  }
}

bool recording_capture(const chirp_capture_t *capture) {
  if (!valid_capture(capture))
    return false;
  capture_record_t *record = free_capture_slot();
  if (!record) {
    ++status.captures_dropped;
    return false;
  }
  pack_capture(record, capture);
  ++status.captures_pending;
  return true;
}

void recording_note(const char *text, size_t length) {
  if (!text || !length)
    return;
  if (length > sizeof(diagnostic) - diagnostic_size) {
    ++status.diagnostic_lines_dropped;
    return;
  }
  memcpy(diagnostic + diagnostic_size, text, length);
  diagnostic_size += length;
  ++diagnostic_lines;
}

recording_status_t recording_status(void) { return status; }

static storage_status_t write_close(const void *data, size_t length) {
  storage_status_t result = storage_write(data, length);
  storage_status_t close = storage_close();
  return result == STORAGE_OK ? close : result;
}

static storage_status_t open_capture(char path[RECORDING_FILENAME_BYTES]) {
  storage_status_t result;
  unsigned attempts = 0U;
  do {
    (void)snprintf(path, RECORDING_FILENAME_BYTES, "C%07lu.BIN",
                   (unsigned long)next_file);
    result = storage_open(path, STORAGE_OPEN_NEW);
    if (result == STORAGE_OK || result == STORAGE_EXISTS)
      next_file = (next_file + 1U) % 10000000U;
  } while (result == STORAGE_EXISTS && ++attempts < 16U);
  return result == STORAGE_EXISTS ? STORAGE_BUSY : result;
}

static storage_status_t write_capture(const capture_record_t *record) {
  size_t count = record->header[1] >> 16;
  size_t chirps = record->header[13];
  size_t bytes = RECORD_HEADER_BYTES + 2U * chirps * count * sizeof(int16_t);
  return write_close(record, bytes);
}

static void record_capture_result(const char *path, storage_status_t result) {
  if (result == STORAGE_OK) {
    ++status.captures_saved;
    memcpy(status.last_capture, path, sizeof(status.last_capture));
  } else if (result != STORAGE_BUSY) {
    ++status.captures_dropped;
  }
}

static storage_status_t save_queued_capture(void) {
  char path[RECORDING_FILENAME_BYTES];
  storage_status_t result = open_capture(path);
  if (result == STORAGE_BUSY)
    return result;

  if (result == STORAGE_OK)
    result = write_capture(&capture_queue[capture_head]);
  record_capture_result(path, result);
  capture_head = (capture_head + 1U) % RECORD_CAPTURE_QUEUE;
  --status.captures_pending;
  return result;
}

static storage_status_t save_diagnostic(void) {
  storage_status_t result = storage_open("DIAG.LOG", STORAGE_OPEN_APPEND);
  if (result == STORAGE_OK) {
    result = write_close(diagnostic, diagnostic_size);
    if (result != STORAGE_OK)
      status.diagnostic_lines_dropped += diagnostic_lines;
    diagnostic_size = 0U;
    diagnostic_lines = 0U;
  }
  last_diagnostic_write = time_ms();
  return result;
}

static void report_write(storage_status_t result) {
  if (result != STORAGE_OK && result != STORAGE_BUSY) {
    status.last_error = result;
    ++status.write_errors;
  }
}

storage_status_t recording_save_capture(const chirp_capture_t *capture,
                                        char path[RECORDING_FILENAME_BYTES]) {
  if (!path || !valid_capture(capture))
    return STORAGE_INVALID_ARGUMENT;
  if (chirp_capture_active() || acquisition_active())
    return STORAGE_BUSY;
  capture_record_t *record = free_capture_slot();
  if (!record)
    return STORAGE_BUSY;

  char name[RECORDING_FILENAME_BYTES];
  storage_status_t result = open_capture(name);
  if (result == STORAGE_OK) {
    /* Pack once for contiguous writes through the SDMMC staging buffer. */
    pack_capture(record, capture);
    result = write_capture(record);
    if (result == STORAGE_OK)
      memcpy(path, name, sizeof(name));
  }
  record_capture_result(name, result);
  report_write(result);
  return result;
}

void recording_task(void) {
  if (chirp_capture_active() || acquisition_active() || !storage_mounted())
    return;
  if (status.captures_pending) {
    report_write(save_queued_capture());
  } else if (diagnostic_size &&
             time_expired(last_diagnostic_write, RECORD_DIAGNOSTIC_INTERVAL_MS)) {
    report_write(save_diagnostic());
  }
}

storage_status_t recording_flush(void) {
  if (chirp_capture_active() || acquisition_active())
    return STORAGE_BUSY;
  if (!storage_mounted())
    return STORAGE_NOT_MOUNTED;
  while (status.captures_pending) {
    storage_status_t result = save_queued_capture();
    if (result != STORAGE_OK) {
      report_write(result);
      return result;
    }
  }
  storage_status_t result = diagnostic_size ? save_diagnostic() : STORAGE_OK;
  report_write(result);
  return result;
}
