#include "board/board_gain.h"
#include "board/board_gpio.h"
#include "board/board_pins.h"
#include "stm32h7xx_ll_dac.h"

/* VREF+ is tied to +3V3A_MCU. DAC1 scales the 12-bit code by VREF+ / 4096. */
#define GAIN_DAC_REFERENCE_MV 3300U
#define GAIN_DAC_STEPS 4096U

static void write_voltage(uint16_t millivolts) {
  uint32_t code = (millivolts * GAIN_DAC_STEPS + GAIN_DAC_REFERENCE_MV / 2U) /
                  GAIN_DAC_REFERENCE_MV;
  LL_DAC_ConvertData12RightAligned(DAC1, LL_DAC_CHANNEL_1, code);
}

void board_gain_init(void) {
  board_gpio_pin(GAIN_DAC_PORT, GAIN_DAC_PIN, LL_GPIO_MODE_ANALOG,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_LOW, 0U, false);
  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_DAC12);
  LL_DAC_Disable(DAC1, LL_DAC_CHANNEL_1);
  LL_DAC_DisableTrigger(DAC1, LL_DAC_CHANNEL_1);
  LL_DAC_ConfigOutput(DAC1, LL_DAC_CHANNEL_1, LL_DAC_OUTPUT_MODE_NORMAL,
                      LL_DAC_OUTPUT_BUFFER_ENABLE, LL_DAC_OUTPUT_CONNECT_GPIO);
  write_voltage(BOARD_GAIN_DEFAULT_MV);
  LL_DAC_Enable(DAC1, LL_DAC_CHANNEL_1);
}

bool board_gain_set_mv(uint16_t millivolts) {
  if (millivolts < BOARD_GAIN_MIN_MV || millivolts > BOARD_GAIN_MAX_MV)
    return false;
  write_voltage(millivolts);
  return true;
}

uint16_t board_gain_get_mv(void) {
  uint32_t code = LL_DAC_RetrieveOutputData(DAC1, LL_DAC_CHANNEL_1);
  return (uint16_t)((code * GAIN_DAC_REFERENCE_MV + GAIN_DAC_STEPS / 2U) /
                    GAIN_DAC_STEPS);
}
