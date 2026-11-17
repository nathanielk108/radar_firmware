#include "board/board_display.h"
#include "board/board_gpio.h"
#include "board/board_pins.h"
#include "stm32h7xx_ll_rcc.h"
#include "stm32h7xx_ll_tim.h"

/* FMC NE1, with the LCD's D/C line wired to A18 on the 16-bit bus. */
#define LCD_COMMAND_ADDR 0x60000000UL
#define LCD_DATA_ADDR 0x60080000UL
#define LCD_FMC_AF 12U
/* platform/clock.c configures PCLK1 at 120 MHz with 2x timer clocking. */
#define BACKLIGHT_TIMER_HZ 240000000U
#define BACKLIGHT_PWM_HZ 20000U
#define BACKLIGHT_PWM_PERIOD (BACKLIGHT_TIMER_HZ / BACKLIGHT_PWM_HZ)

_Static_assert(BACKLIGHT_PWM_PERIOD <= UINT16_MAX,
               "PWM full-on compare must fit TIM4's 16-bit counter");

#define LCD_FMC_PD_PINS                                                        \
  (LL_GPIO_PIN_0 | LL_GPIO_PIN_1 | LL_GPIO_PIN_4 | LL_GPIO_PIN_5 |              \
   LL_GPIO_PIN_7 | LL_GPIO_PIN_8 | LL_GPIO_PIN_9 | LL_GPIO_PIN_10 |              \
   LL_GPIO_PIN_13 | LL_GPIO_PIN_14 | LL_GPIO_PIN_15)
#define LCD_FMC_PE_PINS                                                        \
  (LL_GPIO_PIN_7 | LL_GPIO_PIN_8 | LL_GPIO_PIN_9 | LL_GPIO_PIN_10 |              \
   LL_GPIO_PIN_11 | LL_GPIO_PIN_12 | LL_GPIO_PIN_13 | LL_GPIO_PIN_14 |           \
   LL_GPIO_PIN_15)

static void bus_pin_mode(uint32_t mode) {
  for (uint8_t i = 0; i < 16U; ++i) {
    if (LCD_FMC_PD_PINS & (1UL << i))
      board_gpio_pin(GPIOD, i, mode, LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                     LL_GPIO_SPEED_FREQ_HIGH, LCD_FMC_AF, false);
    if (LCD_FMC_PE_PINS & (1UL << i))
      board_gpio_pin(GPIOE, i, mode, LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                     LL_GPIO_SPEED_FREQ_HIGH, LCD_FMC_AF, false);
  }
}

static void backlight_pin_mode(uint32_t mode) {
  board_gpio_pin(DISPLAY_BL_PORT, DISPLAY_BL_PIN, mode,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_LOW, DISPLAY_BL_AF, false);
}

void board_display_init(void) {
  board_gpio_pin(DISPLAY_RESET_PORT, DISPLAY_RESET_PIN, LL_GPIO_MODE_OUTPUT,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_LOW, 0U, false);
  backlight_pin_mode(LL_GPIO_MODE_OUTPUT);
}

bool board_display_bus_start(void) {
  if (!LL_RCC_PLL1_IsReady() || !LL_RCC_PLL1Q_IsEnabled())
    return false;
  LL_RCC_SetFMCClockSource(LL_RCC_FMC_CLKSOURCE_PLL1Q);
  LL_AHB3_GRP1_EnableClock(LL_AHB3_GRP1_PERIPH_FMC);
  LL_AHB3_GRP1_ForceReset(LL_AHB3_GRP1_PERIPH_FMC);
  LL_AHB3_GRP1_ReleaseReset(LL_AHB3_GRP1_PERIPH_FMC);

  /* Non-muxed 16-bit asynchronous SRAM/8080, default bank addresses. Keep
   * reserved bit 7 at its reset value. Disable speculative/buffered writes
   * through the MPU Device mapping and the FMC write-FIFO disable bit.
   */
  uint32_t control = (FMC_Bank1_R->BTCR[0] & (1UL << 7)) | FMC_BCR1_WFDIS |
                     FMC_BCRx_WREN | FMC_BCRx_MWID_0;
  FMC_Bank1_R->BTCR[0] = control;
  for (unsigned i = 2U; i < 8U; i += 2U)
    CLEAR_BIT(FMC_Bank1_R->BTCR[i], FMC_BCRx_MBKEN);

  /* PLL1Q = 48 MHz. Use three setup and data cycles, with a full 20.8 ns
   * write-hold cycle in mode 1. Confirm timings on the connector at bring-up.
   */
  MODIFY_REG(FMC_Bank1_R->BTCR[1], FMC_BTRx_ADDSET | FMC_BTRx_DATAST |
                                     FMC_BTRx_BUSTURN,
             (3UL << FMC_BTRx_ADDSET_Pos) | (3UL << FMC_BTRx_DATAST_Pos) |
                 (1UL << FMC_BTRx_BUSTURN_Pos));
  bus_pin_mode(LL_GPIO_MODE_ALTERNATE);
  FMC_Bank1_R->BTCR[0] = control | FMC_BCRx_MBKEN | FMC_BCR1_FMCEN;
  __DSB();
  return true;
}

void board_display_bus_stop(void) {
  __DSB();
  CLEAR_BIT(FMC_Bank1_R->BTCR[0], FMC_BCR1_FMCEN | FMC_BCRx_MBKEN);
  bus_pin_mode(LL_GPIO_MODE_ANALOG);
  LL_AHB3_GRP1_DisableClock(LL_AHB3_GRP1_PERIPH_FMC);
}

void board_display_reset(bool asserted) {
  if (asserted)
    LL_GPIO_ResetOutputPin(DISPLAY_RESET_PORT, 1UL << DISPLAY_RESET_PIN);
  else
    LL_GPIO_SetOutputPin(DISPLAY_RESET_PORT, 1UL << DISPLAY_RESET_PIN);
}

void board_display_backlight(uint8_t percent) {
  bool running = LL_APB1_GRP1_IsEnabledClock(LL_APB1_GRP1_PERIPH_TIM4);
  if (percent == 0U) {
    /* Drive the MOSFET gate low before stopping the timer. */
    backlight_pin_mode(LL_GPIO_MODE_OUTPUT);
    if (running) {
      LL_TIM_DisableCounter(TIM4);
      LL_APB1_GRP1_DisableClock(LL_APB1_GRP1_PERIPH_TIM4);
    }
    return;
  }

  uint32_t compare = (BACKLIGHT_PWM_PERIOD * percent) / 100U;
  if (running) {
    LL_TIM_OC_SetCompareCH1(TIM4, compare);
    return;
  }

  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM4);
  LL_APB1_GRP1_ForceReset(LL_APB1_GRP1_PERIPH_TIM4);
  LL_APB1_GRP1_ReleaseReset(LL_APB1_GRP1_PERIPH_TIM4);
  LL_TIM_SetAutoReload(TIM4, BACKLIGHT_PWM_PERIOD - 1U);
  LL_TIM_OC_SetMode(TIM4, LL_TIM_CHANNEL_CH1, LL_TIM_OCMODE_PWM1);
  LL_TIM_OC_EnablePreload(TIM4, LL_TIM_CHANNEL_CH1);
  LL_TIM_OC_SetCompareCH1(TIM4, compare);
  LL_TIM_GenerateEvent_UPDATE(TIM4);
  LL_TIM_CC_EnableChannel(TIM4, LL_TIM_CHANNEL_CH1);
  LL_TIM_EnableCounter(TIM4);
  backlight_pin_mode(LL_GPIO_MODE_ALTERNATE);
}

void board_display_command(uint8_t command) {
  *(volatile uint16_t *)LCD_COMMAND_ADDR = command;
}

void board_display_data(uint16_t data) {
  *(volatile uint16_t *)LCD_DATA_ADDR = data;
}
