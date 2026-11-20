#include "services/chirp.h"

#include "board/board_freq.h"
#include "board/board_gain.h"
#include "board/board_timer.h"
#include "platform/time.h"
#include "stm32h7xx.h"

#define CHIRP_MIN_BANDWIDTH_HZ 150000000U
#define CHIRP_PLL_SETTLE_MS 20U
#define CHIRP_TIMEOUT_MARGIN_MS 5U
#define CHIRP_ADC_START_TICKS 480U
#define CHIRP_TIMER_TICKS_PER_US (ACQUISITION_TIMER_HZ / 1000000U)

_Static_assert(CHIRP_MAX_COUNT <= BOARD_CHIRP_MAX_COUNT,
               "TIM2 must support every ADC burst in the train");

typedef enum { CHIRP_IDLE, CHIRP_SETTLING, CHIRP_RUNNING } chirp_phase_t;

static chirp_phase_t phase;
static chirp_capture_t pending;
static uint32_t phase_started_ms;
static uint32_t timeout_ms;
static uint32_t sequence;
static uint32_t first_sample_us;

bool chirp_capture_active(void) { return phase != CHIRP_IDLE; }

static acquisition_status_t finish(acquisition_status_t status) {
  if (!board_timer_chirp_stop())
    status = ACQUISITION_DMA_ERROR;
  if (acquisition_active()) {
    acquisition_status_t cleanup = acquisition_burst_stop();
    if (status == ACQUISITION_OK)
      status = cleanup;
  }
  adf4158_power_down();
  phase = CHIRP_IDLE;
  return status;
}

static acquisition_status_t start(uint16_t count, uint32_t repetition_us) {
  if (chirp_capture_active() || acquisition_active())
    return ACQUISITION_BUSY;

  adf4158_ramp_config_t profile = board_adf4158_ramp;
  uint32_t rate = profile.external_step_hz;
  if (!rate || rate % ACQUISITION_SAMPLE_RATE_HZ != 0U ||
      ACQUISITION_TIMER_HZ % rate != 0U || !profile.steps ||
      profile.steps > BOARD_CHIRP_MAX_STEPS)
    return ACQUISITION_INVALID_ARGUMENT;

  uint32_t steps_per_sample = rate / ACQUISITION_SAMPLE_RATE_HZ;
  uint32_t samples = profile.steps / steps_per_sample + 1U;
  if (profile.steps % steps_per_sample != 0U ||
      samples > ACQUISITION_MAX_BLOCK_SAMPLES)
    return ACQUISITION_INVALID_ARGUMENT;

  bool train = count > 1U;
  uint16_t stride = (uint16_t)samples;
  acquisition_status_t status = train
      ? acquisition_burst_train_arm((uint16_t)samples, count, &stride)
      : acquisition_burst_arm((uint16_t)samples);
  if (status != ACQUISITION_OK)
    return status;

  uint32_t step_ticks = ACQUISITION_TIMER_HZ / rate;
  uint32_t repetition_ticks = repetition_us * CHIRP_TIMER_TICKS_PER_US;
  uint32_t adc_end = CHIRP_ADC_START_TICKS +
                     (train ? stride : samples + 1U) * ACQUISITION_TIMER_TICKS;
  /* Allow for the TIM2-to-TIM16 DMA start latency before any reset edge. */
  uint32_t settle_edge = adc_end + ACQUISITION_TIMER_TICKS;
  const board_chirp_timing_t timing = {
      .adc_start_ticks = CHIRP_ADC_START_TICKS,
      /* Keep the first and last retained samples halfway between steps. */
      .first_step_ticks =
          CHIRP_ADC_START_TICKS + ACQUISITION_CS_OFFSET_TICKS + step_ticks / 2U,
      .step_period_ticks = step_ticks,
      .step_high_ticks = step_ticks / 2U,
      .end_ticks = train ? repetition_ticks : settle_edge,
      .reset_ticks = train ? settle_edge : 0U,
      .step_count = (uint16_t)profile.steps,
      .chirp_count = count,
  };
  if (!board_timer_chirp_prepare(&timing))
    return finish(ACQUISITION_DMA_ERROR);

  profile.mode = train ? ADF4158_RAMP_SAWTOOTH : ADF4158_RAMP_SINGLE;
  adf4158_ramp_result_t ramp;
  if (!adf4158_program_ramp(&profile, &ramp) ||
      ramp.bandwidth_hz < CHIRP_MIN_BANDWIDTH_HZ)
    return finish(ACQUISITION_INVALID_ARGUMENT);

  pending = (chirp_capture_t){
      .start_hz = profile.pll.frequency_hz,
      .sample_rate_hz = ACQUISITION_SAMPLE_RATE_HZ,
      .step_rate_hz = rate,
      .step_hz = ramp.step_hz,
      .bandwidth_hz = ramp.bandwidth_hz,
      .timer_hz = ACQUISITION_TIMER_HZ,
      .first_sample_offset_ticks = -(int32_t)(step_ticks / 2U),
      .repetition_ticks = repetition_ticks,
      .chirp_count = count,
      .sample_stride = stride,
      .gain_mv = board_gain_get_mv(),
  };
  timeout_ms = CHIRP_TIMEOUT_MARGIN_MS +
               timing.end_ticks * count / (ACQUISITION_TIMER_HZ / 1000U);
  adf4158_start_ramp();
  /* TXDATA stays low while the PLL settles at the initial frequency. */
  phase_started_ms = time_ms();
  phase = CHIRP_SETTLING;
  return ACQUISITION_OK;
}

acquisition_status_t chirp_capture_start(void) { return start(1U, 0U); }

acquisition_status_t chirp_capture_train_start(uint16_t chirp_count,
                                              uint32_t repetition_us) {
  if (chirp_count < 2U || chirp_count > CHIRP_MAX_COUNT ||
      repetition_us < CHIRP_MIN_REPETITION_US ||
      repetition_us > CHIRP_MAX_REPETITION_US)
    return ACQUISITION_INVALID_ARGUMENT;
  return start(chirp_count, repetition_us);
}

acquisition_status_t chirp_capture_poll(chirp_capture_t *capture) {
  if (!capture)
    return ACQUISITION_INVALID_ARGUMENT;
  if (phase == CHIRP_IDLE)
    return ACQUISITION_NOT_READY;
  if (phase == CHIRP_SETTLING) {
    if (time_expired(phase_started_ms, CHIRP_PLL_SETTLE_MS)) {
      phase_started_ms = time_ms();
      phase = CHIRP_RUNNING;
      ++sequence;
      uint32_t irq_state = __get_PRIMASK();
      __disable_irq();
      uint32_t timer_start_us = time_us();
      board_timer_chirp_start();
      __set_PRIMASK(irq_state);
      first_sample_us = timer_start_us +
                        (CHIRP_ADC_START_TICKS + ACQUISITION_CS_OFFSET_TICKS) /
                            CHIRP_TIMER_TICKS_PER_US;
    }
    return ACQUISITION_BUSY;
  }

  if (board_timer_chirp_has_error())
    return finish(ACQUISITION_DMA_ERROR);
  acquisition_status_t status = ACQUISITION_BUSY;
  if (board_timer_chirp_complete())
    status = acquisition_burst_finish(&pending.samples);
  if (status == ACQUISITION_BUSY) {
    if (time_expired(phase_started_ms, timeout_ms))
      return finish(ACQUISITION_TIMEOUT);
    return status;
  }
  status = finish(status);
  if (status == ACQUISITION_OK) {
    pending.samples.sequence = sequence;
    pending.samples.first_sample_us = first_sample_us;
    pending.samples.timestamp_valid = true;
    *capture = pending;
  }
  return status;
}

acquisition_status_t chirp_capture_cancel(void) {
  return chirp_capture_active() ? finish(ACQUISITION_OK) : ACQUISITION_NOT_READY;
}

bool chirp_capture_get_block(const chirp_capture_t *capture, uint16_t index,
                             acquisition_block_t *block) {
  if (!capture || !block || index >= capture->chirp_count ||
      !capture->samples.a || !capture->samples.b || !capture->samples.count ||
      capture->sample_stride < capture->samples.count)
    return false;

  uint32_t offset = (uint32_t)index * capture->sample_stride;
  *block = capture->samples;
  block->a += offset;
  block->b += offset;
  block->first_sample_us +=
      (uint32_t)index * capture->repetition_ticks / CHIRP_TIMER_TICKS_PER_US;
  return true;
}

acquisition_status_t chirp_capture_one(chirp_capture_t *capture) {
  if (!capture)
    return ACQUISITION_INVALID_ARGUMENT;
  acquisition_status_t status = chirp_capture_start();
  if (status != ACQUISITION_OK)
    return status;
  do {
    status = chirp_capture_poll(capture);
  } while (status == ACQUISITION_BUSY);
  return status;
}
