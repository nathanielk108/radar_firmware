#include "drivers/adf4158.h"
#include "board/board_pins.h"
#include "platform/time.h"
#include "stm32h7xx_ll_gpio.h"

#define ADF4158_MODULUS (1ULL << 25)
#define ADF4158_RAMP_EN (1UL << 31)
#define ADF4158_POWER_DOWN (1UL << 5)
#define ADF4158_MAX_PFD_HZ 32000000ULL
#define ADF4158_SYNC_N_WORD (1UL << 15)
#define ADF4158_TX_RAMP_CLK (1UL << 29)

static adf4158_status_t last_status = ADF4158_STATUS_OK;
static uint32_t saved_r0;
static bool ramp_configured;

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

void adf4158_write(uint32_t value) {
  LL_GPIO_ResetOutputPin(FREQ_LE_PORT, 1UL << FREQ_LE_PIN);
  LL_GPIO_ResetOutputPin(FREQ_CLK_PORT, 1UL << FREQ_CLK_PIN);
  time_delay_us(1U);

  for (int bit = 31; bit >= 0; --bit) {
    if ((value >> bit) & 1U)
      LL_GPIO_SetOutputPin(FREQ_DATA_PORT, 1UL << FREQ_DATA_PIN);
    else
      LL_GPIO_ResetOutputPin(FREQ_DATA_PORT, 1UL << FREQ_DATA_PIN);
    time_delay_us(1U);

    LL_GPIO_SetOutputPin(FREQ_CLK_PORT, 1UL << FREQ_CLK_PIN);
    time_delay_us(1U);

    LL_GPIO_ResetOutputPin(FREQ_CLK_PORT, 1UL << FREQ_CLK_PIN);
    time_delay_us(1U);
  }

  LL_GPIO_SetOutputPin(FREQ_LE_PORT, 1UL << FREQ_LE_PIN);
  time_delay_us(1U);
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

  if (ref > ADF4158_MAX_PFD_HZ * denominator || ref < denominator) {
    return false;
  }

  uint64_t n =
      rounded_div(cfg->frequency_hz * denominator * ADF4158_MODULUS, ref);

  uint64_t integer = n >> 25;
  uint32_t frac = (uint32_t)(n & (ADF4158_MODULUS - 1U));

  if (integer < (cfg->prescaler_8_9 ? 75U : 23U) || integer > 4095U) {
    return false;
  }

  *r0 = ((uint32_t)integer << 15) | ((frac >> 13) << 3);
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
  adf4158_stop_ramp();
  adf4158_write(r7);
  adf4158_write(r6);
  adf4158_write((1U << 23) | 6U);
  adf4158_write(r5);
  adf4158_write((r5 & ADF4158_TX_RAMP_CLK) | (1U << 23) | 5U);
  adf4158_write(r4);
  adf4158_write(r3);
  adf4158_write(r2);
  adf4158_write(r1);
  adf4158_write(r0);
}

void adf4158_power_down(void) {
  adf4158_stop_ramp();
  adf4158_write(ADF4158_POWER_DOWN | 3U);
  ramp_configured = false;
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
  return true;
}

bool adf4158_program_ramp(const adf4158_ramp_config_t *cfg,
                          adf4158_ramp_result_t *result) {
  if (cfg == 0 || cfg->steps == 0U || cfg->steps > 10000U ||
      cfg->bandwidth_hz == 0U || cfg->bandwidth_hz > 500000000U ||
      (!cfg->external_step_hz &&
       (cfg->chirp_us == 0U || cfg->chirp_us > 1000000U)) ||
      (unsigned)cfg->mode > ADF4158_RAMP_SINGLE ||
      (cfg->delay_us &&
       (cfg->external_step_hz || cfg->mode != ADF4158_RAMP_SAWTOOTH)) ||
      cfg->pll.frequency_hz > 6100000000ULL - cfg->bandwidth_hz) {
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

  uint64_t pfd_hz = ref / denominator;
  if (pfd_hz > ADF4158_MAX_PFD_HZ) {
    last_status = ADF4158_STATUS_PLL_RANGE;
    return false;
  }

  uint16_t clk1 = 1U;
  uint16_t clk2 = 0U;
  uint64_t step_ns;
  uint64_t chirp_ns;
  if (cfg->external_step_hz) {
    step_ns = rounded_div(1000000000ULL, cfg->external_step_hz);
    chirp_ns = rounded_div(cfg->steps * 1000000000ULL, cfg->external_step_hz);
  } else {
    /* Internal ramp step period = CLK1 * CLK2 / fPFD. */
    uint64_t ticks = rounded_div((uint64_t)cfg->chirp_us * pfd_hz,
                                 (uint64_t)cfg->steps * 1000000ULL);
    if (ticks == 0U || ticks > 4095ULL * 4095ULL) {
      last_status = ADF4158_STATUS_RAMP_TIMING;
      return false;
    }
    for (uint32_t c2 = 1U; c2 <= 4095U; ++c2) {
      if (ticks % c2 == 0U && ticks / c2 <= 4095U) {
        clk1 = (uint16_t)(ticks / c2);
        clk2 = (uint16_t)c2;
        break;
      }
    }
    if (clk2 == 0U) {
      last_status = ADF4158_STATUS_RAMP_TIMING;
      return false;
    }
    step_ns = rounded_div(ticks * 1000000000ULL, pfd_hz);
    chirp_ns = rounded_div(ticks * cfg->steps * 1000000000ULL, pfd_hz);
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
  uint64_t delay_word = 0U;

  if (cfg->delay_us != 0U) {
    delay_word = rounded_div((uint64_t)cfg->delay_us * pfd_hz,
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
    r7 |= (uint32_t)delay_word << 3;
  }

  uint32_t r3 =
      ((uint32_t)cfg->mode << 10) | ((uint32_t)cfg->pll.positive_pd << 6) | 3U;

  uint32_t r4 = 4U;
  uint32_t r5 = ((uint32_t)best_offset << 19) | ((uint32_t)best_dev << 3) | 5U;
  if (cfg->external_step_hz) {
    /* Align INT and FRAC loading at integer boundaries (ADF4158 N SEL). */
    r3 |= ADF4158_SYNC_N_WORD;
    r5 |= ADF4158_TX_RAMP_CLK;
  } else {
    r4 |= (3U << 19) | ((uint32_t)clk2 << 7);
  }

  uint32_t r6 = (cfg->steps << 3) | 6U;

  program_registers(r0, r1, r2, r3, r4, r5, r6, r7);

  saved_r0 = r0;
  ramp_configured = true;

  if (result != 0) {
    uint64_t actual_step = (uint64_t)best_dev * (1ULL << best_offset) * pfd_hz;

    result->step_hz = (uint32_t)rounded_div(actual_step, ADF4158_MODULUS);

    result->bandwidth_hz =
        (uint32_t)rounded_div(actual_step * cfg->steps, ADF4158_MODULUS);

    result->step_ns = step_ns;
    result->chirp_ns = chirp_ns;
    result->delay_ns = rounded_div(delay_word * clk1 * 1000000000ULL, pfd_hz);

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
  if (ramp_configured)
    adf4158_write(saved_r0);
}

void adf4158_start_ramp(void) {
  if (ramp_configured) {
    adf4158_write(saved_r0);
    adf4158_write(saved_r0 | ADF4158_RAMP_EN);
  }
}
