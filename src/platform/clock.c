#include "platform/clock.h"
#include "stm32h7xx.h"

#include <stdbool.h>
#include <stdint.h>

#define CLOCK_TIMEOUT 2000000U

static bool wait_set(volatile uint32_t *reg, uint32_t mask) {
  for (uint32_t i = 0; i < CLOCK_TIMEOUT; ++i) {
    if ((*reg & mask) == mask) {
      return true;
    }
  }
  return false;
}

static bool wait_clear(volatile uint32_t *reg, uint32_t mask) {
  for (uint32_t i = 0; i < CLOCK_TIMEOUT; ++i) {
    if ((*reg & mask) == 0U) {
      return true;
    }
  }
  return false;
}

static bool configure_vos0(void) {
  MODIFY_REG(PWR->D3CR, PWR_D3CR_VOS, PWR_D3CR_VOS);

  if (!wait_set(&PWR->CSR1, PWR_CSR1_ACTVOSRDY)) {
    return false;
  }

  // SYSCFG must be enabled before ODEN can be used
  RCC->APB4ENR |= RCC_APB4ENR_SYSCFGEN;
  (void)RCC->APB4ENR;

  // VOS1 + ODEN = VOS0
  SYSCFG->PWRCR |= SYSCFG_PWRCR_ODEN;

  if (!wait_set(&PWR->CSR1, PWR_CSR1_ACTVOSRDY)) {
    return false;
  }

  return true;
}

static void configure_flash(void) {
  MODIFY_REG(
    FLASH->ACR,
    FLASH_ACR_LATENCY_Msk | FLASH_ACR_WRHIGHFREQ_Msk,
    FLASH_ACR_LATENCY_4WS | FLASH_ACR_WRHIGHFREQ_1
  );

  while ((FLASH->ACR & FLASH_ACR_LATENCY_Msk) != FLASH_ACR_LATENCY_4WS) { }
}

static bool enable_hse(void) {
  RCC->CR |= RCC_CR_HSEON;

  return wait_set(&RCC->CR, RCC_CR_HSERDY);
}

static bool configure_pll1(void) {
  RCC->CR &= ~RCC_CR_PLL1ON;

  if (!wait_clear(&RCC->CR, RCC_CR_PLL1RDY)) {
    return false;
  }

  MODIFY_REG(
    RCC->PLLCKSELR,
    RCC_PLLCKSELR_PLLSRC_Msk | RCC_PLLCKSELR_DIVM1_Msk,
    RCC_PLLCKSELR_PLLSRC_HSE | (5U << RCC_PLLCKSELR_DIVM1_Pos)
  );

  MODIFY_REG(
    RCC->PLLCFGR,
    RCC_PLLCFGR_PLL1RGE_Msk | RCC_PLLCFGR_PLL1VCOSEL_Msk | RCC_PLLCFGR_PLL1FRACEN_Msk | RCC_PLLCFGR_DIVP1EN_Msk,
    RCC_PLLCFGR_PLL1RGE_2 | RCC_PLLCFGR_DIVP1EN
  );

  RCC->PLL1DIVR = ((192U - 1U) << RCC_PLL1DIVR_N1_Pos) |
                  ((2U - 1U)   << RCC_PLL1DIVR_P1_Pos) |
                  ((20U - 1U)  << RCC_PLL1DIVR_Q1_Pos) |
                  ((2U - 1U)   << RCC_PLL1DIVR_R1_Pos);

  RCC->PLL1FRACR = 0U;

  RCC->CR |= RCC_CR_PLL1ON;

  if (!wait_set(&RCC->CR, RCC_CR_PLL1RDY)) {
    return false;
  }

  return true;
}

static void configure_bus_clocks(void) {
  MODIFY_REG(
    RCC->D1CFGR,
    RCC_D1CFGR_D1CPRE_Msk | RCC_D1CFGR_HPRE_Msk | RCC_D1CFGR_D1PPRE_Msk,
    RCC_D1CFGR_D1CPRE_DIV1 | RCC_D1CFGR_HPRE_DIV2 | RCC_D1CFGR_D1PPRE_DIV2
  );

  MODIFY_REG(
    RCC->D2CFGR,
    RCC_D2CFGR_D2PPRE1_Msk | RCC_D2CFGR_D2PPRE2_Msk,
    RCC_D2CFGR_D2PPRE1_DIV2 | RCC_D2CFGR_D2PPRE2_DIV2
  );

  MODIFY_REG(
    RCC->D3CFGR,
    RCC_D3CFGR_D3PPRE_Msk,
    RCC_D3CFGR_D3PPRE_DIV2
  );
}

static bool select_pll1_sysclk(void) {
  MODIFY_REG(
    RCC->CFGR,
    RCC_CFGR_SW_Msk,
    RCC_CFGR_SW_PLL1
  );

  for (uint32_t i = 0; i < CLOCK_TIMEOUT; ++i) {
    if ((RCC->CFGR & RCC_CFGR_SWS_Msk) == RCC_CFGR_SWS_PLL1) {
      return true;
    }
  }

  return false;
}

// Initialize MCU clock to 480 MHz
clock_status_t clock_init(void) {
  const uint16_t revision = (uint16_t)(DBGMCU->IDCODE >> 16);

  if (revision != 0x2001U && revision != 0x2003U) {
    return CLOCK_ERR_SILICON_REV;
  }

  if (!configure_vos0()) {
    return CLOCK_ERR_VOS;
  }

  configure_flash();

  if (!enable_hse()) {
    return CLOCK_ERR_HSE;
  }

  configure_bus_clocks();

  if (!configure_pll1()) {
    return CLOCK_ERR_PLL;
  }

  if (!select_pll1_sysclk()) {
    return CLOCK_ERR_SYSCLK;
  }

  SystemCoreClockUpdate();

  return CLOCK_OK;
}
