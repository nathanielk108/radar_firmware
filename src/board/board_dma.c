#include "board/board_dma.h"

#include "stm32h7xx_ll_bus.h"
#include "stm32h7xx_ll_dma.h"
#include "stm32h7xx_ll_spi.h"

#include <stdbool.h>

void board_dma_adc_init(uint16_t *tx, uint16_t *rx_a, uint16_t *rx_b) {
  LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA2);

  LL_DMA_DisableStream(DMA2, LL_DMA_STREAM_0);
  LL_DMA_DisableStream(DMA2, LL_DMA_STREAM_1);
  LL_DMA_DisableStream(DMA2, LL_DMA_STREAM_2);

  LL_DMA_ConfigTransfer(
      DMA2, LL_DMA_STREAM_0,
      LL_DMA_DIRECTION_MEMORY_TO_PERIPH | LL_DMA_PRIORITY_HIGH |
          LL_DMA_MODE_NORMAL | LL_DMA_PERIPH_NOINCREMENT |
          LL_DMA_MEMORY_INCREMENT | LL_DMA_PDATAALIGN_HALFWORD |
          LL_DMA_MDATAALIGN_HALFWORD);
  LL_DMA_ConfigAddresses(DMA2, LL_DMA_STREAM_0, (uint32_t)tx,
                         (uint32_t) & (SPI5->TXDR),
                         LL_DMA_DIRECTION_MEMORY_TO_PERIPH);
  LL_DMA_SetPeriphRequest(DMA2, LL_DMA_STREAM_0, LL_DMAMUX1_REQ_SPI5_TX);

  LL_DMA_ConfigTransfer(
      DMA2, LL_DMA_STREAM_1,
      LL_DMA_DIRECTION_PERIPH_TO_MEMORY | LL_DMA_PRIORITY_HIGH |
          LL_DMA_MODE_NORMAL | LL_DMA_PERIPH_NOINCREMENT |
          LL_DMA_MEMORY_INCREMENT | LL_DMA_PDATAALIGN_HALFWORD |
          LL_DMA_MDATAALIGN_HALFWORD);
  LL_DMA_ConfigAddresses(DMA2, LL_DMA_STREAM_1, (uint32_t) & (SPI5->RXDR),
                         (uint32_t)rx_a, LL_DMA_DIRECTION_PERIPH_TO_MEMORY);
  LL_DMA_SetPeriphRequest(DMA2, LL_DMA_STREAM_1, LL_DMAMUX1_REQ_SPI5_RX);

  LL_DMA_ConfigTransfer(
      DMA2, LL_DMA_STREAM_2,
      LL_DMA_DIRECTION_PERIPH_TO_MEMORY | LL_DMA_PRIORITY_HIGH |
          LL_DMA_MODE_NORMAL | LL_DMA_PERIPH_NOINCREMENT |
          LL_DMA_MEMORY_INCREMENT | LL_DMA_PDATAALIGN_HALFWORD |
          LL_DMA_MDATAALIGN_HALFWORD);
  LL_DMA_ConfigAddresses(DMA2, LL_DMA_STREAM_2, (uint32_t) & (SPI4->RXDR),
                         (uint32_t)rx_b, LL_DMA_DIRECTION_PERIPH_TO_MEMORY);
  LL_DMA_SetPeriphRequest(DMA2, LL_DMA_STREAM_2, LL_DMAMUX1_REQ_SPI4_RX);

  NVIC_SetPriority(DMA2_Stream0_IRQn, 4U);
  NVIC_EnableIRQ(DMA2_Stream0_IRQn);

  NVIC_SetPriority(DMA2_Stream1_IRQn, 4U);
  NVIC_EnableIRQ(DMA2_Stream1_IRQn);

  NVIC_SetPriority(DMA2_Stream2_IRQn, 4U);
  NVIC_EnableIRQ(DMA2_Stream2_IRQn);

  LL_DMA_EnableIT_TC(DMA2, LL_DMA_STREAM_0);
  LL_DMA_EnableIT_TE(DMA2, LL_DMA_STREAM_0);
  LL_DMA_EnableIT_TC(DMA2, LL_DMA_STREAM_1);
  LL_DMA_EnableIT_TE(DMA2, LL_DMA_STREAM_1);
  LL_DMA_EnableIT_TC(DMA2, LL_DMA_STREAM_2);
  LL_DMA_EnableIT_TE(DMA2, LL_DMA_STREAM_2);
}
