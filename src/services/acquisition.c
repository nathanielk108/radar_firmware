#include "board/board_dma.h"
#include "board/board_pins.h"
#include "drivers/ad4683.h"
#include "platform/time.h"

#include "services/acquisition.h"

#include "stm32h7xx_ll_dma.h"
#include "stm32h7xx_ll_gpio.h"
#include "stm32h7xx_ll_spi.h"

#define DMA_BUFFER __attribute__((section(".dma_buffer"), aligned(32)))

DMA_BUFFER static uint16_t adc_tx[1];
DMA_BUFFER static uint16_t adc_a[1];
DMA_BUFFER static uint16_t adc_b[1];

#define ADC_DMA_TX_DONE (1U << 0)
#define ADC_DMA_RX_A_DONE (1U << 1)
#define ADC_DMA_RX_B_DONE (1U << 2)
#define ADC_DMA_ERROR (1U << 3)
#define ADC_DMA_ALL_DONE                                                       \
  (ADC_DMA_TX_DONE | ADC_DMA_RX_A_DONE | ADC_DMA_RX_B_DONE)

volatile static uint8_t adc_dma_events;

static void clear_flags(void) {
  LL_DMA_ClearFlag_TC0(DMA2);
  LL_DMA_ClearFlag_TC1(DMA2);
  LL_DMA_ClearFlag_TC2(DMA2);
  LL_DMA_ClearFlag_TE0(DMA2);
  LL_DMA_ClearFlag_TE1(DMA2);
  LL_DMA_ClearFlag_TE2(DMA2);
  adc_dma_events = 0U;
}

static void start(void) {
  LL_SPI_EnableDMAReq_TX(SPI5);
  LL_SPI_EnableDMAReq_RX(SPI5);
  LL_SPI_EnableDMAReq_RX(SPI4);

  LL_SPI_Disable(SPI4);
  LL_SPI_Disable(SPI5);

  LL_SPI_SetTransferSize(SPI5, 1U);
  LL_SPI_SetTransferSize(SPI4, 1U);

  LL_DMA_EnableStream(DMA2, LL_DMA_STREAM_1);
  LL_DMA_EnableStream(DMA2, LL_DMA_STREAM_2);
  LL_DMA_EnableStream(DMA2, LL_DMA_STREAM_0);

  LL_SPI_Enable(SPI4);
  LL_SPI_Enable(SPI5);
  LL_GPIO_ResetOutputPin(ADC_A_CS_PORT, 1U << ADC_A_CS_PIN);
  LL_SPI_StartMasterTransfer(SPI5);
}

static void stop(void) {
  LL_SPI_SuspendMasterTransfer(SPI5);
  LL_GPIO_SetOutputPin(ADC_A_CS_PORT, 1U << ADC_A_CS_PIN);
  LL_SPI_Disable(SPI5);
  LL_SPI_Disable(SPI4);

  LL_DMA_DisableStream(DMA2, LL_DMA_STREAM_0);
  LL_DMA_DisableStream(DMA2, LL_DMA_STREAM_1);
  LL_DMA_DisableStream(DMA2, LL_DMA_STREAM_2);

  LL_SPI_DisableDMAReq_TX(SPI5);
  LL_SPI_DisableDMAReq_RX(SPI5);
  LL_SPI_DisableDMAReq_RX(SPI4);
}

acquisition_status_t acquisition_init(void) {

  adc_tx[0] = 0U;
  adc_a[0] = 0U;
  adc_b[0] = 0U;

  board_dma_adc_init(adc_tx, adc_a, adc_b);

  return ACQUISITION_OK;
}

acquisition_status_t acquisition_capture_one(ad4683_sample_t *sample,
                                             uint32_t timeout_ms) {
  clear_flags();
  start();

  uint32_t start = time_ms();

  while (!time_expired(start, timeout_ms)) {
    if ((adc_dma_events & ADC_DMA_ERROR) != 0U) {
      // DMA error
      stop();
      return ACQUISITION_DMA_ERROR;
    }

    if (LL_SPI_IsActiveFlag_OVR(SPI5)) {
      // SPI error
      stop();
      return ACQUISITION_SPI_ERROR;
    }

    if ((adc_dma_events & ADC_DMA_ALL_DONE) == ADC_DMA_ALL_DONE &&
        LL_SPI_IsActiveFlag_EOT(SPI5)) {
      // Transfer complete
      sample->a = (int16_t)adc_a[0];
      sample->b = (int16_t)adc_b[0];
      stop();
      return ACQUISITION_OK;
    }
  }

  stop();
  return ACQUISITION_TIMEOUT;
}

// ADC Transmit
void DMA2_Stream0_IRQHandler(void) {
  if (LL_DMA_IsActiveFlag_TC0(DMA2)) {
    LL_DMA_ClearFlag_TC0(DMA2);
    adc_dma_events |= ADC_DMA_TX_DONE;
  }

  if (LL_DMA_IsActiveFlag_TE0(DMA2)) {
    LL_DMA_ClearFlag_TE0(DMA2);
    adc_dma_events |= ADC_DMA_ERROR;
  }
}

// ADC Channel A
void DMA2_Stream1_IRQHandler(void) {
  if (LL_DMA_IsActiveFlag_TC1(DMA2)) {
    LL_DMA_ClearFlag_TC1(DMA2);
    adc_dma_events |= ADC_DMA_RX_A_DONE;
  }

  if (LL_DMA_IsActiveFlag_TE1(DMA2)) {
    LL_DMA_ClearFlag_TE1(DMA2);
    adc_dma_events |= ADC_DMA_ERROR;
  }
}

// ADC Channel B
void DMA2_Stream2_IRQHandler(void) {
  if (LL_DMA_IsActiveFlag_TC2(DMA2)) {
    LL_DMA_ClearFlag_TC2(DMA2);
    adc_dma_events |= ADC_DMA_RX_B_DONE;
  }

  if (LL_DMA_IsActiveFlag_TE2(DMA2)) {
    LL_DMA_ClearFlag_TE2(DMA2);
    adc_dma_events |= ADC_DMA_ERROR;
  }
}
