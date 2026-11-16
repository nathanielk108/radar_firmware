#include "board/board_usb_cc.h"
#include "board/board_gpio.h"
#include "board/board_pins.h"
#include "platform/time.h"
#include "stm32h7xx_ll_adc.h"
#include "stm32h7xx_ll_rcc.h"

#define ADC_CC1_CHANNEL LL_ADC_CHANNEL_6
#define ADC_CC2_CHANNEL LL_ADC_CHANNEL_2
#define ADC_REFERENCE_MV 3300U
#define ADC_STEPS 1024U
#define ADC_TIMEOUT_MS 2U

static bool wait_adc(volatile const uint32_t *reg, uint32_t mask,
                     uint32_t value) {
  uint32_t start = time_ms();
  while ((*reg & mask) != value) {
    if (time_expired(start, ADC_TIMEOUT_MS))
      return false;
  }
  return true;
}

bool board_usb_cc_init(void) {
  board_gpio_pin(USB_CC_PORT, USB_CC1_PIN, LL_GPIO_MODE_ANALOG,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_LOW, 0U, false);
  board_gpio_pin(USB_CC_PORT, USB_CC2_PIN, LL_GPIO_MODE_ANALOG,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_LOW, 0U, false);

  /* HSI64 / 16 gives 4 MHz without ADC boost or an additional PLL. */
  LL_RCC_SetCLKPClockSource(LL_RCC_CLKP_CLKSOURCE_HSI);
  LL_RCC_SetADCClockSource(LL_RCC_ADC_CLKSOURCE_CLKP);
  LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_ADC12);
  LL_ADC_SetCommonClock(ADC12_COMMON, LL_ADC_CLOCK_ASYNC_DIV16);
  LL_ADC_DisableDeepPowerDown(ADC1);
  LL_ADC_EnableInternalRegulator(ADC1);
  time_delay_us(LL_ADC_DELAY_INTERNAL_REGUL_STAB_US);

  /* 10 bits are sufficient for CC and remain valid if VDDA exceeds 3.3 V. */
  LL_ADC_SetResolution(ADC1, LL_ADC_RESOLUTION_10B);
  LL_ADC_SetChannelPreselection(ADC1, ADC_CC1_CHANNEL);
  LL_ADC_SetChannelPreselection(ADC1, ADC_CC2_CHANNEL);
  /* 64.5 cycles at 4 MHz allow the 5.1 kohm Rd network to settle. */
  LL_ADC_SetChannelSamplingTime(ADC1, ADC_CC1_CHANNEL,
                                LL_ADC_SAMPLINGTIME_64CYCLES_5);
  LL_ADC_SetChannelSamplingTime(ADC1, ADC_CC2_CHANNEL,
                                LL_ADC_SAMPLINGTIME_64CYCLES_5);
  LL_ADC_StartCalibration(ADC1, LL_ADC_CALIB_OFFSET, LL_ADC_SINGLE_ENDED);
  if (!wait_adc(&ADC1->CR, ADC_CR_ADCAL, 0U))
    return false;

  time_delay_us(2U); /* At least 4 ADC clocks after calibration. */
  LL_ADC_ClearFlag_ADRDY(ADC1);
  LL_ADC_Enable(ADC1);
  return wait_adc(&ADC1->ISR, ADC_ISR_ADRDY, ADC_ISR_ADRDY);
}

static bool read_channel_mv(uint32_t channel, uint16_t *millivolts) {
  if (!wait_adc(&ADC1->CR, ADC_CR_ADSTART, 0U))
    return false;
  /* One software-triggered regular conversion at a time. */
  LL_ADC_REG_SetSequencerRanks(ADC1, LL_ADC_REG_RANK_1, channel);
  LL_ADC_ClearFlag_EOC(ADC1);
  LL_ADC_REG_StartConversion(ADC1);
  if (!wait_adc(&ADC1->ISR, ADC_ISR_EOC, ADC_ISR_EOC))
    return false;

  uint32_t code = LL_ADC_REG_ReadConversionData10(ADC1);
  *millivolts =
      (uint16_t)((code * ADC_REFERENCE_MV + ADC_STEPS / 2U) / ADC_STEPS);
  return true;
}

bool board_usb_cc_read_mv(uint16_t *cc1_mv, uint16_t *cc2_mv) {
  if (!cc1_mv || !cc2_mv || !LL_ADC_IsActiveFlag_ADRDY(ADC1))
    return false;

  uint16_t cc1, cc2;
  if (!read_channel_mv(ADC_CC1_CHANNEL, &cc1) ||
      !read_channel_mv(ADC_CC2_CHANNEL, &cc2))
    return false;

  *cc1_mv = cc1;
  *cc2_mv = cc2;
  return true;
}
