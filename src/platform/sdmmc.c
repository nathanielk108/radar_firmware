#include "platform/sdmmc.h"

#include "board/board_gpio.h"
#include "board/board_pins.h"
#include "platform/time.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_ll_rcc.h"
#include <string.h>

#define SDMMC_CLOCK_HZ 48000000U
#define SDMMC_TIMEOUT_MS 1000U
#define SDMMC_STAGE_SECTORS 8U
#define SDMMC_DATA_ERRORS                                                      \
  (SDMMC_FLAG_DCRCFAIL | SDMMC_FLAG_DTIMEOUT | SDMMC_FLAG_TXUNDERR |             \
   SDMMC_FLAG_RXOVERR | SDMMC_FLAG_DABORT | SDMMC_FLAG_IDMATE)

static SD_HandleTypeDef sd;
static bool socket_ready;
static bool card_ready;

/* The firmware linker puts .bss in AXI SRAM. Keep entire private cache lines
 * here so SDMMC1 IDMA can transfer without touching any other object. */
_Alignas(32) static uint8_t stage[SDMMC_STAGE_SECTORS * SDMMC_SECTOR_SIZE];

uint32_t HAL_GetTick(void) { return time_ms(); }

void HAL_Delay(uint32_t delay) { time_delay_ms(delay); }

uint32_t HAL_RCCEx_GetPeriphCLKFreq(uint64_t peripheral) {
  if (peripheral != RCC_PERIPHCLK_SDMMC ||
      LL_RCC_GetSDMMCClockSource(LL_RCC_SDMMC_CLKSOURCE) !=
          LL_RCC_SDMMC_CLKSOURCE_PLL1Q ||
      (RCC->CR & RCC_CR_PLL1RDY) == 0U ||
      (RCC->PLLCFGR & RCC_PLLCFGR_DIVQ1EN) == 0U)
    return 0U;
  return SDMMC_CLOCK_HZ;
}

void sdmmc_socket_init(void) {
  if (socket_ready)
    return;
  for (uint8_t pin = STORAGE_SD_D0_PIN; pin <= STORAGE_SD_D3_PIN; ++pin)
    board_gpio_pin(STORAGE_SD_DATA_PORT, pin, LL_GPIO_MODE_ALTERNATE,
                   LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_UP,
                   LL_GPIO_SPEED_FREQ_VERY_HIGH, STORAGE_SD_AF, true);
  board_gpio_pin(STORAGE_SD_DATA_PORT, STORAGE_SD_CLK_PIN,
                 LL_GPIO_MODE_ALTERNATE, LL_GPIO_OUTPUT_PUSHPULL,
                 LL_GPIO_PULL_NO, LL_GPIO_SPEED_FREQ_VERY_HIGH, STORAGE_SD_AF,
                 false);
  board_gpio_pin(STORAGE_SD_CMD_PORT, STORAGE_SD_CMD_PIN, LL_GPIO_MODE_ALTERNATE,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_UP,
                 LL_GPIO_SPEED_FREQ_VERY_HIGH, STORAGE_SD_AF, true);
  board_gpio_pin(STORAGE_CD_PORT, STORAGE_CD_PIN, LL_GPIO_MODE_INPUT,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_UP,
                 LL_GPIO_SPEED_FREQ_LOW, 0U, false);
  socket_ready = true;
}

bool sdmmc_present(void) {
  return socket_ready &&
         !LL_GPIO_IsInputPinSet(STORAGE_CD_PORT, 1UL << STORAGE_CD_PIN);
}

bool sdmmc_ready(void) { return card_ready && sdmmc_present(); }

void sdmmc_reset(void) {
  if (LL_AHB3_GRP1_IsEnabledClock(LL_AHB3_GRP1_PERIPH_SDMMC1)) {
    LL_AHB3_GRP1_ForceReset(LL_AHB3_GRP1_PERIPH_SDMMC1);
    LL_AHB3_GRP1_ReleaseReset(LL_AHB3_GRP1_PERIPH_SDMMC1);
    LL_AHB3_GRP1_DisableClock(LL_AHB3_GRP1_PERIPH_SDMMC1);
  }
  card_ready = false;
}

bool sdmmc_card_init(void) {
  if (!sdmmc_present())
    return false;
  if (card_ready)
    return true;

  LL_RCC_SetSDMMCClockSource(LL_RCC_SDMMC_CLKSOURCE_PLL1Q);
  LL_AHB3_GRP1_EnableClock(LL_AHB3_GRP1_PERIPH_SDMMC1);
  /* FatFs is foreground-only. Poll IDMA completion so SD cannot add ISR
   * latency to time-critical peripherals. */
  NVIC_DisableIRQ(SDMMC1_IRQn);
  NVIC_ClearPendingIRQ(SDMMC1_IRQn);
  sd = (SD_HandleTypeDef){
      .Instance = SDMMC1,
      .Init = {.ClockEdge = SDMMC_CLOCK_EDGE_RISING,
               .ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE,
               .BusWide = SDMMC_BUS_WIDE_4B,
               .HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE,
               .ClockDiv = 2U},
  };
  /* The vendor driver identifies the card on one bit at <=400 kHz first. */
  if (HAL_SD_Init(&sd) != HAL_OK || !sdmmc_present()) {
    sdmmc_reset();
    return false;
  }
  card_ready = true;
  return true;
}

uint32_t sdmmc_sector_count(void) {
  return sdmmc_ready() ? sd.SdCard.LogBlockNbr : 0U;
}

bool sdmmc_sync(void) {
  uint32_t started = time_ms();
  while (sdmmc_ready()) {
    if (HAL_SD_GetCardState(&sd) == HAL_SD_CARD_TRANSFER)
      return true;
    if (time_expired(started, SDMMC_TIMEOUT_MS))
      break;
    time_delay_ms(1U);
  }
  sdmmc_reset();
  return false;
}

static bool await_transfer(void) {
  uint32_t started = time_ms();
  while (sdmmc_present() && !time_expired(started, SDMMC_TIMEOUT_MS)) {
    uint32_t status = SDMMC1->STA;
    if (status & SDMMC_DATA_ERRORS)
      return false;
    if (status & SDMMC1->MASK)
      HAL_SD_IRQHandler(&sd);
    if (HAL_SD_GetState(&sd) == HAL_SD_STATE_READY)
      return HAL_SD_GetError(&sd) == HAL_SD_ERROR_NONE;
  }
  return false;
}

static bool transfer(uint32_t sector, uint32_t count, uint8_t *read_data,
                     const uint8_t *write_data) {
  uint32_t total = sdmmc_sector_count();
  if ((!read_data && !write_data) || !count || sector >= total ||
      count > total - sector)
    return false;

  while (count) {
    uint32_t blocks = count > SDMMC_STAGE_SECTORS ? SDMMC_STAGE_SECTORS : count;
    int32_t bytes = (int32_t)(blocks * SDMMC_SECTOR_SIZE);
    HAL_StatusTypeDef status;
    if (write_data) {
      memcpy(stage, write_data, (size_t)bytes);
      SCB_CleanDCache_by_Addr(stage, bytes);
      status = HAL_SD_WriteBlocks_DMA(&sd, stage, sector, blocks);
    } else {
      SCB_InvalidateDCache_by_Addr(stage, bytes);
      status = HAL_SD_ReadBlocks_DMA(&sd, stage, sector, blocks);
    }
    if (status != HAL_OK || !await_transfer()) {
      if (sdmmc_present())
        (void)HAL_SD_Abort(&sd);
      sdmmc_reset();
      return false;
    }
    if (!sdmmc_sync())
      return false;
    if (write_data) {
      write_data += bytes;
    } else {
      SCB_InvalidateDCache_by_Addr(stage, bytes);
      memcpy(read_data, stage, (size_t)bytes);
      read_data += bytes;
    }
    sector += blocks;
    count -= blocks;
  }
  return true;
}

bool sdmmc_read(uint32_t sector, uint32_t count, uint8_t *data) {
  return transfer(sector, count, data, NULL);
}

bool sdmmc_write(uint32_t sector, uint32_t count, const uint8_t *data) {
  return transfer(sector, count, NULL, data);
}
