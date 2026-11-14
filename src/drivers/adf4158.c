#include <stdio.h>

#include "board/board_pins.h"
#include "drivers/adf4158.h"
#include "stm32h7xx_ll_gpio.h"

#define ADF4158_MODULUS (1ULL << 25)
#define ADF4158_RAMP_EN (1UL << 31)

static adf4158_status_t last_status = ADF4158_STATUS_OK;
static uint32_t saved_r0;
static bool configured = false;
static bool ramp_active;
static bool ramp_configured;
static uint32_t diagnostic_start_mhz;
static uint32_t diagnostic_bandwidth_mhz;
static uint32_t diagnostic_chirp_us;
static uint32_t diagnostic_delay_us;

adf4158_status_t adf4158_last_status(void) { return last_status; }

const char *adf4158_status_name(adf4158_status_t status) {
  switch (status) {
  case ADF4158_STATUS_OK:
    return "OK (write-only; lock unverified)";
  case ADF4158_STATUS_INVALID_CONFIG:
    return "invalid configuration";
  case ADF4158_STATUS_PLL_RANGE:
    return "invalid PLL divider/frequency";
  case ADF4158_STATUS_RAMP_TIMING:
    return "ramp timing cannot be represented";
  case ADF4158_STATUS_DEVIATION:
    return "deviation outside register range";
  case ADF4158_STATUS_RAMP_DELAY:
    return "inter-ramp delay outside register range";
  default:
    return "unknown";
  }
}

static void adf4158_delay(void) {
  uint32_t cycles = SystemCoreClock / 1000000U;
  if (cycles == 0U)
    cycles = 1U;

  uint32_t start = DWT->CYCCNT;
  while ((uint32_t)(DWT->CYCCNT - start) < cycles) {
  }
}

void adf4158_write(uint32_t value) {
  LL_GPIO_ResetOutputPin(FREQ_LE_PORT, 1UL << FREQ_LE_PIN);
  LL_GPIO_ResetOutputPin(FREQ_CLK_PORT, 1UL << FREQ_CLK_PIN);
  adf4158_delay();

  for (int bit = 31; bit >= 0; --bit) {
    if ((value >> bit) & 1U)
      LL_GPIO_SetOutputPin(FREQ_DATA_PORT, 1UL << FREQ_DATA_PIN);
    else
      LL_GPIO_ResetOutputPin(FREQ_DATA_PORT, 1UL << FREQ_DATA_PIN);
    adf4158_delay();

    LL_GPIO_SetOutputPin(FREQ_CLK_PORT, 1UL << FREQ_CLK_PIN);
    adf4158_delay();

    LL_GPIO_ResetOutputPin(FREQ_CLK_PORT, 1UL << FREQ_CLK_PIN);
    adf4158_delay();
  }

  adf4158_delay();
  LL_GPIO_SetOutputPin(FREQ_LE_PORT, 1UL << FREQ_LE_PIN);
  adf4158_delay();
  LL_GPIO_ResetOutputPin(FREQ_LE_PORT, 1UL << FREQ_LE_PIN);
}

static uint64_t rounded_div(uint64_t a, uint64_t b) { return (a + b / 2U) / b; }

static bool pll_registers(const adf4158_fixed_config_t *cfg, uint16_t clk1,
                          uint32_t *r0, uint32_t *r1, uint32_t *r2) {
  if (cfg == 0 || cfg->reference_hz == 0U || cfg->frequency_hz < 500000000ULL ||
      cfg->frequency_hz > 6100000000ULL || cfg->r_counter < 1U ||
      cfg->r_counter > 32U || cfg->cp_current > 15U || clk1 == 0U ||
      clk1 > 4095U ||
      (cfg->frequency_hz > 3000000000ULL && !cfg->prescaler_8_9) ||
      (cfg->reference_doubler && cfg->reference_hz > 30000000U)) {
    return false;
  }

  uint64_t ref =
      (uint64_t)cfg->reference_hz * (cfg->reference_doubler ? 2U : 1U);

  uint32_t denominator = cfg->r_counter * (cfg->reference_div2 ? 2U : 1U);

  if (ref > 32000000ULL * denominator || ref < denominator) {
    return false;
  }

  uint64_t n =
      rounded_div(cfg->frequency_hz * denominator * ADF4158_MODULUS, ref);

  uint32_t integer = (uint32_t)(n >> 25);
  uint32_t frac = (uint32_t)(n & (ADF4158_MODULUS - 1U));

  if (integer < (cfg->prescaler_8_9 ? 75U : 23U) || integer > 4095U) {
    return false;
  }

  *r0 = (integer << 15) | ((frac >> 13) << 3);
  *r1 = ((frac & 0x1FFFU) << 15) | 1U;

  *r2 = ((uint32_t)cfg->cp_current << 24) |
        ((uint32_t)cfg->prescaler_8_9 << 22) |
        ((uint32_t)cfg->reference_div2 << 21) |
        ((uint32_t)cfg->reference_doubler << 20) |
        (((uint32_t)cfg->r_counter & 0x1FU) << 15) | ((uint32_t)clk1 << 3) | 2U;

  return true;
}

static void program_registers(uint32_t r0, uint32_t r1, uint32_t r2,
                              uint32_t r3, uint32_t r4, uint32_t r5,
                              uint32_t r6, uint32_t r7) {
  adf4158_write(r7);
  adf4158_write(r6);
  adf4158_write((1U << 23) | 6U);
  adf4158_write(r5);
  adf4158_write((1U << 23) | 5U);
  adf4158_write(r4);
  adf4158_write(r3);
  adf4158_write(r2);
  adf4158_write(r1);
  adf4158_write(r0);

  saved_r0 = r0 & ~ADF4158_RAMP_EN;
  configured = true;
  ramp_active = false;
}

bool adf4158_program_fixed(const adf4158_fixed_config_t *cfg) {
  uint32_t r0, r1, r2;

  if (!pll_registers(cfg, 1U, &r0, &r1, &r2)) {
    last_status = ADF4158_STATUS_PLL_RANGE;
    return false;
  }

  uint32_t r3 = ((uint32_t)cfg->positive_pd << 6) | 3U;

  program_registers(r0, r1, r2, r3, 4U, 5U, 6U, 7U);
  ramp_configured = false;
  last_status = ADF4158_STATUS_OK;
  diagnostic_start_mhz = (uint32_t)(cfg->frequency_hz / 1000000ULL);
  return true;
}

bool adf4158_program_ramp(const adf4158_ramp_config_t *cfg,
                          adf4158_ramp_result_t *result) {
  if (cfg == 0 || cfg->steps == 0U || cfg->steps > 10000U ||
      cfg->bandwidth_hz == 0U || cfg->bandwidth_hz > 500000000U ||
      cfg->chirp_us == 0U || cfg->chirp_us > 1000000U ||
      cfg->mode > ADF4158_RAMP_SINGLE ||
      (cfg->delay_us != 0U && cfg->mode != ADF4158_RAMP_SAWTOOTH) ||
      cfg->pll.frequency_hz + cfg->bandwidth_hz > 6100000000ULL) {
    last_status = ADF4158_STATUS_INVALID_CONFIG;
    return false;
  }

  uint64_t ref =
      (uint64_t)cfg->pll.reference_hz * (cfg->pll.reference_doubler ? 2U : 1U);

  uint32_t denominator =
      cfg->pll.r_counter * (cfg->pll.reference_div2 ? 2U : 1U);

  if (denominator == 0U || ref < denominator) {
    last_status = ADF4158_STATUS_INVALID_CONFIG;
    return false;
  }

  uint32_t pfd_hz = (uint32_t)(ref / denominator);

  if (pfd_hz == 0U) {
    last_status = ADF4158_STATUS_PLL_RANGE;
    return false;
  }

  // Ramp step period = CLK1 * CLK2 / fPFD.
  uint64_t ticks = rounded_div((uint64_t)cfg->chirp_us * pfd_hz,
                               (uint64_t)cfg->steps * 1000000ULL);

  if (ticks == 0U || ticks > 4095ULL * 4095ULL) {
    last_status = ADF4158_STATUS_RAMP_TIMING;
    return false;
  }

  uint16_t clk1 = 0;
  uint16_t clk2 = 0;

  for (uint32_t c2 = 1U; c2 <= 4095U; ++c2) {
    if (ticks % c2 == 0U && ticks / c2 <= 4095U) {
      clk1 = (uint16_t)(ticks / c2);
      clk2 = (uint16_t)c2;
      break;
    }
  }

  if (clk1 == 0U) {
    last_status = ADF4158_STATUS_RAMP_TIMING;
    return false;
  }

  uint32_t r0, r1, r2;

  if (!pll_registers(&cfg->pll, clk1, &r0, &r1, &r2)) {
    last_status = ADF4158_STATUS_PLL_RANGE;
    return false;
  }

  // Find deviation and offset with minimum bandwidth error.
  uint64_t desired = (uint64_t)cfg->bandwidth_hz * ADF4158_MODULUS;

  uint64_t best_err = UINT64_MAX;
  uint16_t best_dev = 0;
  uint8_t best_offset = 0;

  for (uint8_t offset = 0U; offset <= 15U; ++offset) {
    uint64_t unit = (uint64_t)cfg->steps * pfd_hz * (1ULL << offset);

    uint64_t dev = rounded_div(desired, unit);

    if (dev == 0U || dev > 32767U) {
      continue;
    }

    uint64_t actual = dev * unit;
    uint64_t error = actual > desired ? actual - desired : desired - actual;

    if (error < best_err) {
      best_err = error;
      best_dev = (uint16_t)dev;
      best_offset = offset;
    }
  }

  if (best_dev == 0U) {
    last_status = ADF4158_STATUS_DEVIATION;
    return false;
  }

  // R7: delay between sawtooth ramps.
  // Delay = CLK1 * delay_word / fPFD.
  uint32_t delay_word = 0U;

  if (cfg->delay_us != 0U) {
    delay_word = (uint32_t)rounded_div((uint64_t)cfg->delay_us * pfd_hz,
                                       (uint64_t)clk1 * 1000000ULL);

    if (delay_word == 0U || delay_word > 4095U) {
      last_status = ADF4158_STATUS_RAMP_DELAY;
      return false;
    }
  }

  uint32_t r7 = 7U;

  if (delay_word != 0U) {
    r7 |= (1U << 17); // Ramp delay enable
    r7 |= (1U << 16); // PFD * CLK1 delay clock
    r7 |= (delay_word << 3);
  }

  uint32_t r3 =
      ((uint32_t)cfg->mode << 10) | ((uint32_t)cfg->pll.positive_pd << 6) | 3U;

  uint32_t r4 = (3U << 19) | ((uint32_t)clk2 << 7) | 4U;

  uint32_t r5 = ((uint32_t)best_offset << 19) | ((uint32_t)best_dev << 3) | 5U;

  uint32_t r6 = (cfg->steps << 3) | 6U;

  program_registers(r0, r1, r2, r3, r4, r5, r6, r7);

  diagnostic_start_mhz = (uint32_t)(cfg->pll.frequency_hz / 1000000ULL);
  diagnostic_bandwidth_mhz = cfg->bandwidth_hz / 1000000U;
  diagnostic_chirp_us = cfg->chirp_us;
  diagnostic_delay_us = cfg->delay_us;
  ramp_configured = true;
  adf4158_start_ramp();

  if (result != 0) {
    uint64_t actual_step = (uint64_t)best_dev * (1ULL << best_offset) * pfd_hz;

    result->step_hz = (uint32_t)rounded_div(actual_step, ADF4158_MODULUS);

    result->bandwidth_hz =
        (uint32_t)rounded_div(actual_step * cfg->steps, ADF4158_MODULUS);

    result->step_ns = (uint32_t)rounded_div(ticks * 1000000000ULL, pfd_hz);

    result->chirp_ns =
        (uint32_t)rounded_div(ticks * cfg->steps * 1000000000ULL, pfd_hz);

    result->delay_ns = (uint32_t)rounded_div(
        (uint64_t)delay_word * clk1 * 1000000000ULL, pfd_hz);

    result->interval_ns = result->chirp_ns + result->delay_ns;

    result->deviation = best_dev;
    result->deviation_offset = best_offset;
    result->clk1 = clk1;
    result->clk2 = clk2;
  }

  last_status = ADF4158_STATUS_OK;
  return true;
}

void adf4158_stop_ramp(void) {
  if (configured) {
    adf4158_write(saved_r0);
    ramp_active = false;
  }
}

void adf4158_start_ramp(void) {
  if (configured) {
    adf4158_write(saved_r0);
    adf4158_write(saved_r0 | ADF4158_RAMP_EN);
    ramp_active = true;
  }
}

const char *adf4158_diagnostic(char *buf, size_t size) {
  if (!buf || !size)
    return "ADF4158: invalid buffer";
  if (last_status != ADF4158_STATUS_OK) {
    (void)snprintf(buf, size, "ADF4158: %s (last programming attempt)",
                   adf4158_status_name(last_status));
  } else if (!configured) {
    (void)snprintf(buf, size, "ADF4158: not programmed (no readback)");
  } else if (ramp_configured) {
    (void)snprintf(
        buf, size,
        "ADF4158: ramp=%s start=%luMHz BW=%luMHz chirp=%luus delay=%luus "
        "lock=unverified",
        ramp_active ? "on" : "off", (unsigned long)diagnostic_start_mhz,
        (unsigned long)diagnostic_bandwidth_mhz,
        (unsigned long)diagnostic_chirp_us, (unsigned long)diagnostic_delay_us);
  } else {
    (void)snprintf(buf, size, "ADF4158: fixed=%luMHz lock=unverified",
                   (unsigned long)diagnostic_start_mhz);
  }
  return buf;
}
