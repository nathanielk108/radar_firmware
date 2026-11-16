#include "platform/clock.h"
#include "stm32h7xx.h"

#include <stdbool.h>
#include <stdint.h>

/* clock_init runs before SysTick; use the already-enabled DWT CYCCNT.
   SystemCoreClock describes the reset clock until SYSCLK is switched. */
#define CLOCK_TIMEOUT_MS 100U

static bool clock_timed_out(uint32_t start) {
  uint32_t cycles = (SystemCoreClock / 1000U) * CLOCK_TIMEOUT_MS;
  return (uint32_t)(DWT->CYCCNT - start) >= cycles;
}

static bool wait_mask(volatile uint32_t *reg, uint32_t mask, uint32_t value) {
  uint32_t start = DWT->CYCCNT;
  while ((*reg & mask) != value) {
    if (clock_timed_out(start))
      return false;
  }
  return true;
}

static bool configure_vos0(void) {
  MODIFY_REG(PWR->D3CR, PWR_D3CR_VOS, PWR_D3CR_VOS);
  if (!wait_mask(&PWR->CSR1, PWR_CSR1_ACTVOSRDY, PWR_CSR1_ACTVOSRDY))
    return false;

  // SYSCFG must be enabled before ODEN can be used
  RCC->APB4ENR |= RCC_APB4ENR_SYSCFGEN;
  (void)RCC->APB4ENR;

  // VOS1 + ODEN = VOS0
  SYSCFG->PWRCR |= SYSCFG_PWRCR_ODEN;

  return wait_mask(&PWR->CSR1, PWR_CSR1_ACTVOSRDY, PWR_CSR1_ACTVOSRDY);
}

static bool configure_flash(void) {
  MODIFY_REG(FLASH->ACR, FLASH_ACR_LATENCY_Msk | FLASH_ACR_WRHIGHFREQ_Msk,
             FLASH_ACR_LATENCY_4WS | FLASH_ACR_WRHIGHFREQ_1);

  return wait_mask(&FLASH->ACR,
                   FLASH_ACR_LATENCY_Msk | FLASH_ACR_WRHIGHFREQ_Msk,
                   FLASH_ACR_LATENCY_4WS | FLASH_ACR_WRHIGHFREQ_1);
}

static bool configure_pll1(void) {
  RCC->CR &= ~RCC_CR_PLL1ON;

  if (!wait_mask(&RCC->CR, RCC_CR_PLL1RDY, 0U))
    return false;

  MODIFY_REG(RCC->PLLCKSELR, RCC_PLLCKSELR_PLLSRC_Msk | RCC_PLLCKSELR_DIVM1_Msk,
             RCC_PLLCKSELR_PLLSRC_HSE | (5U << RCC_PLLCKSELR_DIVM1_Pos));

  MODIFY_REG(RCC->PLLCFGR,
             RCC_PLLCFGR_PLL1RGE_Msk | RCC_PLLCFGR_PLL1VCOSEL_Msk |
                 RCC_PLLCFGR_PLL1FRACEN_Msk | RCC_PLLCFGR_DIVP1EN_Msk |
                 RCC_PLLCFGR_DIVQ1EN_Msk,
             RCC_PLLCFGR_PLL1RGE_2 | RCC_PLLCFGR_DIVP1EN | RCC_PLLCFGR_DIVQ1EN);

  RCC->PLL1DIVR = ((192U - 1U) << RCC_PLL1DIVR_N1_Pos) |
                  ((2U - 1U) << RCC_PLL1DIVR_P1_Pos) |
                  ((20U - 1U) << RCC_PLL1DIVR_Q1_Pos) |
                  ((2U - 1U) << RCC_PLL1DIVR_R1_Pos);

  RCC->PLL1FRACR = 0U;

  RCC->CR |= RCC_CR_PLL1ON;

  return wait_mask(&RCC->CR, RCC_CR_PLL1RDY, RCC_CR_PLL1RDY);
}

static void configure_bus_clocks(void) {
  MODIFY_REG(
      RCC->D1CFGR,
      RCC_D1CFGR_D1CPRE_Msk | RCC_D1CFGR_HPRE_Msk | RCC_D1CFGR_D1PPRE_Msk,
      RCC_D1CFGR_D1CPRE_DIV1 | RCC_D1CFGR_HPRE_DIV2 | RCC_D1CFGR_D1PPRE_DIV2);

  MODIFY_REG(RCC->D2CFGR, RCC_D2CFGR_D2PPRE1_Msk | RCC_D2CFGR_D2PPRE2_Msk,
             RCC_D2CFGR_D2PPRE1_DIV2 | RCC_D2CFGR_D2PPRE2_DIV2);

  MODIFY_REG(RCC->D3CFGR, RCC_D3CFGR_D3PPRE_Msk, RCC_D3CFGR_D3PPRE_DIV2);
}

static void configure_peripheral_clocks(void) {
  /* SPI1/2/3 use PLL1Q (48 MHz); SPI4/5 use PCLK2 (120 MHz). */
  CLEAR_BIT(RCC->D2CCIP1R, RCC_D2CCIP1R_SPI123SEL | RCC_D2CCIP1R_SPI45SEL);
  /* I2C4 uses PCLK4. APB timer clocks run at twice PCLK. */
  CLEAR_BIT(RCC->D3CCIPR, RCC_D3CCIPR_I2C4SEL);
  CLEAR_BIT(RCC->CFGR, RCC_CFGR_TIMPRE);
}

// Initialize MCU clock to 480 MHz
clock_status_t clock_init(void) {
  const uint16_t revision = (uint16_t)(DBGMCU->IDCODE >> 16);

  /* ES0392 specifies 480 MHz for revisions X and V. */
  if (revision != 0x2001U && revision != 0x2003U)
    return CLOCK_ERR_SILICON_REV;

  if (!configure_vos0())
    return CLOCK_ERR_VOS;
  if (!configure_flash())
    return CLOCK_ERR_FLASH;

  SET_BIT(RCC->CR, RCC_CR_HSEON);
  if (!wait_mask(&RCC->CR, RCC_CR_HSERDY, RCC_CR_HSERDY))
    return CLOCK_ERR_HSE;

  configure_bus_clocks();
  configure_peripheral_clocks();

  if (!configure_pll1())
    return CLOCK_ERR_PLL;

  MODIFY_REG(RCC->CFGR, RCC_CFGR_SW_Msk, RCC_CFGR_SW_PLL1);
  if (!wait_mask(&RCC->CFGR, RCC_CFGR_SWS_Msk, RCC_CFGR_SWS_PLL1))
    return CLOCK_ERR_SYSCLK;

  SystemCoreClockUpdate();
  return CLOCK_OK;
}
