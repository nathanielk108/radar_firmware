#include "services/chirp.h"

#include "board/board_freq.h"
#include "board/board_timer.h"
#include "platform/time.h"

#define CHIRP_MIN_BANDWIDTH_HZ 150000000U
#define CHIRP_PLL_SETTLE_MS 20U
#define CHIRP_CAPTURE_TIMEOUT_MS 5U

acquisition_status_t chirp_capture_one(chirp_capture_t *capture) {
  const adf4158_ramp_config_t *profile = &board_adf4158_ramp;
  uint32_t rate = profile->external_step_hz;
  if (!capture || !rate || rate % ACQUISITION_SAMPLE_RATE_HZ != 0U ||
      ACQUISITION_TIMER_HZ % rate != 0U ||
      profile->mode != ADF4158_RAMP_SINGLE || !profile->steps ||
      profile->steps > BOARD_CHIRP_MAX_STEPS)
    return ACQUISITION_INVALID_ARGUMENT;
  uint32_t steps_per_sample = rate / ACQUISITION_SAMPLE_RATE_HZ;
  uint32_t samples = profile->steps / steps_per_sample + 1U;
  if (profile->steps % steps_per_sample != 0U ||
      samples > ACQUISITION_MAX_BLOCK_SAMPLES)
    return ACQUISITION_INVALID_ARGUMENT;

  uint32_t step_ticks = ACQUISITION_TIMER_HZ / rate;
  const uint32_t adc_start_ticks = 480U;
  const board_chirp_timing_t timing = {
      .adc_start_ticks = adc_start_ticks,
      /* Center ADC samples between rising edges, including the endpoints. */
      .first_step_ticks =
          adc_start_ticks + ACQUISITION_CS_OFFSET_TICKS + step_ticks / 2U,
      .step_period_ticks = step_ticks,
      .step_high_ticks = step_ticks / 2U,
      /* One extra ADC frame drains the pipeline. Allow for trigger DMA delay.
       */
      .end_ticks = adc_start_ticks + (samples + 1U) * ACQUISITION_TIMER_TICKS +
                   ACQUISITION_TIMER_TICKS,
      .step_count = (uint16_t)profile->steps,
  };

  acquisition_status_t status = acquisition_burst_arm((uint16_t)samples);
  if (status != ACQUISITION_OK)
    return status;
  if (!board_timer_chirp_prepare(&timing)) {
    (void)acquisition_burst_stop();
    return ACQUISITION_DMA_ERROR;
  }

  adf4158_ramp_result_t ramp;
  if (!adf4158_program_ramp(profile, &ramp) ||
      ramp.bandwidth_hz < CHIRP_MIN_BANDWIDTH_HZ) {
    (void)board_timer_chirp_stop();
    (void)acquisition_burst_stop();
    adf4158_power_down();
    return ACQUISITION_INVALID_ARGUMENT;
  }
  adf4158_start_ramp();
  /* With TXDATA low the PLL stays at the initial frequency. */
  time_delay_ms(CHIRP_PLL_SETTLE_MS);

  uint32_t start = time_ms();
  board_timer_chirp_start();
  while (!board_timer_chirp_complete() && !board_timer_chirp_has_error() &&
         !time_expired(start, CHIRP_CAPTURE_TIMEOUT_MS)) {
  }
  if (board_timer_chirp_has_error())
    status = ACQUISITION_DMA_ERROR;
  else if (!board_timer_chirp_complete())
    status = ACQUISITION_TIMEOUT;
  if (!board_timer_chirp_stop())
    status = ACQUISITION_DMA_ERROR;

  if (status == ACQUISITION_OK) {
    do {
      status = acquisition_burst_finish(&capture->samples);
    } while (status == ACQUISITION_BUSY &&
             !time_expired(start, CHIRP_CAPTURE_TIMEOUT_MS));
    if (status == ACQUISITION_BUSY)
      status = ACQUISITION_TIMEOUT;
  }
  adf4158_power_down();
  if (status != ACQUISITION_OK) {
    (void)acquisition_burst_stop();
    return status;
  }

  capture->sample_rate_hz = ACQUISITION_SAMPLE_RATE_HZ;
  capture->step_rate_hz = rate;
  capture->step_hz = ramp.step_hz;
  return ACQUISITION_OK;
}
