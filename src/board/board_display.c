#include "board/board_display.h"
#include "board/board_gpio.h"
#include "board/board_pins.h"
#include "stm32h7xx_ll_rcc.h"

/* FMC NE1, with the LCD's D/C line wired to A18 on the 16-bit bus. */
#define LCD_COMMAND_ADDR 0x60000000UL
#define LCD_DATA_ADDR 0x60080000UL
#define LCD_FMC_AF 12U
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

void board_display_init(void) {
  board_gpio_pin(DISPLAY_RESET_PORT, DISPLAY_RESET_PIN, LL_GPIO_MODE_OUTPUT,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_LOW, 0U, false);
  board_gpio_pin(DISPLAY_BL_PORT, DISPLAY_BL_PIN, LL_GPIO_MODE_OUTPUT,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_LOW, 0U, false);
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

void board_display_backlight(bool enabled) {
  if (enabled)
    LL_GPIO_SetOutputPin(DISPLAY_BL_PORT, 1UL << DISPLAY_BL_PIN);
  else
    LL_GPIO_ResetOutputPin(DISPLAY_BL_PORT, 1UL << DISPLAY_BL_PIN);
}

void board_display_command(uint8_t command) {
  *(volatile uint16_t *)LCD_COMMAND_ADDR = command;
}

void board_display_data(uint16_t data) {
  *(volatile uint16_t *)LCD_DATA_ADDR = data;
}
