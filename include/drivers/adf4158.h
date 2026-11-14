#pragma once
#include <stddef.h>

#include <stdbool.h>
#include <stdint.h>

#define ADF4158_REFERENCE_HZ 10000000U
#define ADF4158_CP_CURRENT 15U

typedef enum {
  ADF4158_STATUS_OK = 0,
  ADF4158_STATUS_INVALID_CONFIG,
  ADF4158_STATUS_PLL_RANGE,
  ADF4158_STATUS_RAMP_TIMING,
  ADF4158_STATUS_DEVIATION,
  ADF4158_STATUS_RAMP_DELAY
} adf4158_status_t;

adf4158_status_t adf4158_last_status(void);
const char *adf4158_status_name(adf4158_status_t status);

typedef struct {
  uint32_t reference_hz;
  uint64_t frequency_hz;
  uint8_t r_counter;
  uint8_t cp_current;
  bool reference_doubler;
  bool reference_div2;
  bool prescaler_8_9;
  bool positive_pd;
} adf4158_fixed_config_t;

typedef enum {
  ADF4158_RAMP_SAWTOOTH = 0,
  ADF4158_RAMP_TRIANGLE = 1,
  ADF4158_RAMP_SINGLE_SAWTOOTH = 2,
  ADF4158_RAMP_SINGLE = 3
} adf4158_ramp_mode_t;

typedef struct {
  adf4158_fixed_config_t pll;
  uint32_t bandwidth_hz;
  uint32_t chirp_us;
  uint32_t delay_us; // 0 disables inter-ramp delay
  uint32_t steps;
  adf4158_ramp_mode_t mode;
} adf4158_ramp_config_t;

typedef struct {
  uint32_t step_hz;
  uint32_t bandwidth_hz;
  uint32_t step_ns;
  uint32_t chirp_ns;
  uint32_t delay_ns;
  uint32_t interval_ns;
  uint16_t deviation;
  uint8_t deviation_offset;
  uint16_t clk1;
  uint16_t clk2;
} adf4158_ramp_result_t;

void adf4158_write(uint32_t value);

bool adf4158_program_fixed(const adf4158_fixed_config_t *config);
bool adf4158_program_ramp(const adf4158_ramp_config_t *config,
                          adf4158_ramp_result_t *result);

void adf4158_stop_ramp(void);
void adf4158_start_ramp(void);

/* Returns the caller-owned buffer; do not call from an ISR. */
const char *adf4158_diagnostic(char *buf, size_t size);
