#include "services/acquisition.h"

#include "board/board_dma.h"
#include "board/board_pins.h"
#include "board/board_spi.h"
#include "board/board_timer.h"
#include "platform/spi.h"
#include "platform/time.h"

#include "stm32h7xx_ll_dma.h"
#include "stm32h7xx_ll_gpio.h"
#include "stm32h7xx_ll_spi.h"
#include "stm32h7xx_ll_tim.h"

#include <stddef.h>

/* These clocks are defined by platform/clock.c. Update both together. */
#define ADC_TIM_CLOCK_HZ 240000000U
#define ADC_MAX_SAMPLE_RATE_HZ 400000U
#define ADC_CS_LOW_TICKS 216U   /* 0.9 us at 240 MHz */
#define ADC_CS_QUIET_TICKS 360U /* AD4683 tQUIET = 1.5 us */
#define ADC_STOP_TIMEOUT_MS 2U
#define DMA_BUFFER __attribute__((section(".dma_buffer"), aligned(32)))

#define DMA_TX LL_DMA_STREAM_0
#define DMA_RX_A LL_DMA_STREAM_1
#define DMA_RX_B LL_DMA_STREAM_2

#define DMA_RX_A_DONE (1U << 0)
#define DMA_RX_B_DONE (1U << 1)
#define DMA_ERROR (1U << 2)
#define DMA_ALL_DONE (DMA_RX_A_DONE | DMA_RX_B_DONE)

#define DMA_ERROR_FLAGS(n)                                                     \
  (DMA_LISR_TEIF##n | DMA_LISR_DMEIF##n | DMA_LISR_FEIF##n)
#define DMA_STREAM_FLAGS(n)                                                    \
  (DMA_ERROR_FLAGS(n) | DMA_LISR_TCIF##n | DMA_LISR_HTIF##n)

/* .dma_buffer is NOLOAD. Initialize TX and publish RX only after DMA fills it.
 */
DMA_BUFFER static uint16_t adc_dummy_tx;
DMA_BUFFER static volatile uint16_t single_rx_a;
DMA_BUFFER static volatile uint16_t single_rx_b;
DMA_BUFFER static uint16_t timed_rx_a[2][ACQUISITION_MAX_BLOCK_SAMPLES];
DMA_BUFFER static uint16_t timed_rx_b[2][ACQUISITION_MAX_BLOCK_SAMPLES];

typedef enum { ACQ_UNINITIALIZED, ACQ_IDLE, ACQ_BUSY, ACQ_FAULT } acq_state_t;

typedef enum { ACQ_MODE_SINGLE, ACQ_MODE_TIMED } acq_mode_t;

static volatile acq_state_t state = ACQ_UNINITIALIZED;
static volatile acq_mode_t mode = ACQ_MODE_SINGLE;
static volatile uint32_t single_events;
static volatile uint32_t timed_a_completed;
static volatile uint32_t timed_b_completed;
static volatile uint32_t timed_pair_completed;
static volatile acquisition_status_t timed_fault;
static volatile uint32_t timed_released;
static uint32_t timed_delivered;
static uint16_t timed_block_samples;
static uint32_t timed_timer_period;

static void adc_cs_high(void) {
  LL_GPIO_SetOutputPin(ADC_A_CS_PORT, 1UL << ADC_A_CS_PIN);
}

static void adc_cs_low(void) {
  LL_GPIO_ResetOutputPin(ADC_A_CS_PORT, 1UL << ADC_A_CS_PIN);
}

static void adc_cs_gpio(void) {
  /* Preload high before selecting GPIO output. */
  adc_cs_high();
  LL_GPIO_SetPinMode(ADC_A_CS_PORT, 1UL << ADC_A_CS_PIN, LL_GPIO_MODE_OUTPUT);
}

static void adc_cs_timer(void) {
  /* PF6 = TIM16_CH1 (AF1). TIM16 output must be disabled/idle high. */
  adc_cs_high();
  LL_GPIO_SetAFPin_0_7(ADC_A_CS_PORT, 1UL << ADC_A_CS_PIN, ADC_A_CS_AF);
  LL_GPIO_SetPinMode(ADC_A_CS_PORT, 1UL << ADC_A_CS_PIN,
                     LL_GPIO_MODE_ALTERNATE);
}

static void dma_clear_flags(void) {
  DMA2->LIFCR = DMA_STREAM_FLAGS(0) | DMA_STREAM_FLAGS(1) | DMA_STREAM_FLAGS(2);
}

static void spi_clear_flags(SPI_TypeDef *spi) {
  spi->IFCR = SPI_IFCR_EOTC | SPI_IFCR_TXTFC | SPI_IFCR_OVRC | SPI_IFCR_MODFC |
              SPI_IFCR_SUSPC;
}

static bool spi_has_error(void) {
  return LL_SPI_IsActiveFlag_OVR(SPI5) || LL_SPI_IsActiveFlag_MODF(SPI5) ||
         LL_SPI_IsActiveFlag_OVR(SPI4) || LL_SPI_IsActiveFlag_MODF(SPI4);
}

static void spi_dma_disable_requests(void) {
  LL_SPI_DisableDMAReq_TX(SPI5);
  LL_SPI_DisableDMAReq_RX(SPI5);
  LL_SPI_DisableDMAReq_RX(SPI4);
}

static bool dma_disable_stream(uint32_t stream) {
  LL_DMA_DisableStream(DMA2, stream);
  uint32_t started = time_ms();
  while (LL_DMA_IsEnabledStream(DMA2, stream)) {
    if (time_expired(started, ADC_STOP_TIMEOUT_MS))
      return false;
  }
  return true;
}

static bool dma_disable_all(void) {
  bool tx = dma_disable_stream(DMA_TX);
  bool a = dma_disable_stream(DMA_RX_A);
  bool b = dma_disable_stream(DMA_RX_B);
  return tx && a && b;
}

static bool spi_disable_one(SPI_TypeDef *spi) {
  LL_SPI_Disable(spi);
  uint32_t started = time_ms();
  while (LL_SPI_IsEnabled(spi)) {
    if (time_expired(started, ADC_STOP_TIMEOUT_MS))
      return false;
  }
  return true;
}

static bool spi_disable_both(void) {
  bool master = spi_disable_one(SPI5);
  bool slave = spi_disable_one(SPI4);
  return master && slave;
}

static void dma_clear_pending_irqs(void) {
  dma_clear_flags();
  NVIC_ClearPendingIRQ(DMA2_Stream0_IRQn);
  NVIC_ClearPendingIRQ(DMA2_Stream1_IRQn);
  NVIC_ClearPendingIRQ(DMA2_Stream2_IRQn);
}

static void dma_config_tx(uint32_t request, uint32_t operating_mode) {
  LL_DMA_ConfigTransfer(
      DMA2, DMA_TX,
      LL_DMA_DIRECTION_MEMORY_TO_PERIPH | LL_DMA_PRIORITY_HIGH |
          operating_mode | LL_DMA_PERIPH_NOINCREMENT |
          LL_DMA_MEMORY_NOINCREMENT | LL_DMA_PDATAALIGN_HALFWORD |
          LL_DMA_MDATAALIGN_HALFWORD);
  LL_DMA_ConfigAddresses(DMA2, DMA_TX, (uint32_t)(uintptr_t)&adc_dummy_tx,
                         (uint32_t)(uintptr_t)&SPI5->TXDR,
                         LL_DMA_DIRECTION_MEMORY_TO_PERIPH);
  LL_DMA_SetPeriphRequest(DMA2, DMA_TX, request);
  LL_DMA_SetDataLength(DMA2, DMA_TX, 1U);

  /* Only RX completion is needed, even for a single conversion. */
  LL_DMA_DisableIT_TC(DMA2, DMA_TX);
  LL_DMA_EnableIT_TE(DMA2, DMA_TX);
  LL_DMA_EnableIT_DME(DMA2, DMA_TX);
  LL_DMA_EnableIT_FE(DMA2, DMA_TX);
}

static void dma_config_rx(uint32_t stream, uint32_t request, SPI_TypeDef *spi,
                          volatile uint16_t *memory0, uint16_t *memory1,
                          uint16_t count) {
  uint32_t flags = LL_DMA_DIRECTION_PERIPH_TO_MEMORY |
                   LL_DMA_PRIORITY_VERYHIGH | LL_DMA_PERIPH_NOINCREMENT |
                   LL_DMA_PDATAALIGN_HALFWORD | LL_DMA_MDATAALIGN_HALFWORD;
  if (memory1) {
    flags |= LL_DMA_MODE_CIRCULAR | LL_DMA_DOUBLEBUFFER_MODE_ENABLE |
             LL_DMA_CURRENTTARGETMEM0 | LL_DMA_MEMORY_INCREMENT;
  }
  LL_DMA_ConfigTransfer(DMA2, stream, flags);
  LL_DMA_ConfigAddresses(DMA2, stream, (uint32_t)(uintptr_t)&spi->RXDR,
                         (uint32_t)(uintptr_t)memory0,
                         LL_DMA_DIRECTION_PERIPH_TO_MEMORY);
  if (memory1)
    LL_DMA_SetMemory1Address(DMA2, stream, (uint32_t)(uintptr_t)memory1);
  LL_DMA_SetDataLength(DMA2, stream, count);
  LL_DMA_SetPeriphRequest(DMA2, stream, request);

  LL_DMA_EnableIT_TC(DMA2, stream);
  LL_DMA_EnableIT_TE(DMA2, stream);
  LL_DMA_EnableIT_DME(DMA2, stream);
  LL_DMA_EnableIT_FE(DMA2, stream);
}

static void dma_config_single(void) {
  adc_dummy_tx = 0U;
  __DMB();
  dma_config_tx(LL_DMAMUX1_REQ_SPI5_TX, LL_DMA_MODE_NORMAL);
  dma_config_rx(DMA_RX_A, LL_DMAMUX1_REQ_SPI5_RX, SPI5, &single_rx_a, NULL, 1U);
  dma_config_rx(DMA_RX_B, LL_DMAMUX1_REQ_SPI4_RX, SPI4, &single_rx_b, NULL, 1U);
}

static bool spi_suspend_master(void) {
  if ((SPI5->CR1 & SPI_CR1_CSTART) == 0U)
    return true;
  LL_SPI_SuspendMasterTransfer(SPI5);
  uint32_t started = time_ms();
  while ((SPI5->CR1 & SPI_CR1_CSTART) != 0U) {
    if (time_expired(started, ADC_STOP_TIMEOUT_MS))
      return false;
  }
  LL_SPI_ClearFlag_SUSP(SPI5);
  return true;
}

static acquisition_status_t spi_dma_shutdown(bool abort) {
  bool master_stopped = true;
  if (abort)
    master_stopped = spi_suspend_master();
  spi_dma_disable_requests();
  bool spi_stopped = spi_disable_both();
  bool dma_stopped = dma_disable_all();
  spi_clear_flags(SPI5);
  spi_clear_flags(SPI4);
  dma_clear_pending_irqs();
  if (!dma_stopped)
    return ACQUISITION_DMA_ERROR;
  if (!master_stopped || !spi_stopped)
    return ACQUISITION_SPI_ERROR;
  return ACQUISITION_OK;
}

static void single_start(void) {
  LL_DMA_SetDataLength(DMA2, DMA_TX, 1U);
  LL_DMA_SetDataLength(DMA2, DMA_RX_A, 1U);
  LL_DMA_SetDataLength(DMA2, DMA_RX_B, 1U);
  dma_clear_pending_irqs();
  single_events = 0U;
  LL_DMA_EnableStream(DMA2, DMA_RX_A);
  LL_DMA_EnableStream(DMA2, DMA_RX_B);
  LL_DMA_EnableStream(DMA2, DMA_TX);
  LL_SPI_EnableDMAReq_RX(SPI4);
  LL_SPI_EnableDMAReq_RX(SPI5);
  LL_SPI_EnableDMAReq_TX(SPI5);
  LL_SPI_Enable(SPI4);
  LL_SPI_Enable(SPI5);
  adc_cs_low();
  time_delay_us(1U);
  LL_SPI_StartMasterTransfer(SPI5);
}

const char *acquisition_status_name(acquisition_status_t status) {
  switch (status) {
  case ACQUISITION_OK:
    return "OK";
  case ACQUISITION_BUSY:
    return "busy/no block";
  case ACQUISITION_TIMEOUT:
    return "timeout";
  case ACQUISITION_DMA_ERROR:
    return "DMA error";
  case ACQUISITION_SPI_ERROR:
    return "SPI error";
  case ACQUISITION_INVALID_ARGUMENT:
    return "invalid argument";
  case ACQUISITION_NOT_READY:
    return "not ready";
  case ACQUISITION_OVERRUN:
    return "buffer overrun";
  default:
    return "unknown";
  }
}

acquisition_status_t acquisition_init(void) {
  if (state == ACQ_BUSY || mode == ACQ_MODE_TIMED)
    return ACQUISITION_BUSY;
  if (state == ACQ_IDLE)
    return ACQUISITION_OK;

  board_dma_adc_init();
  adc_cs_gpio();
  spi_dma_disable_requests();
  bool spi_stopped = spi_disable_both();
  bool dma_stopped = dma_disable_all();
  if (!spi_stopped || !dma_stopped) {
    state = ACQ_FAULT;
    return ACQUISITION_NOT_READY;
  }
  dma_config_single();
  dma_clear_pending_irqs();
  single_events = 0U;
  board_dma_adc_enable_irqs();
  mode = ACQ_MODE_SINGLE;
  state = ACQ_IDLE;
  return ACQUISITION_OK;
}

bool acquisition_is_busy(void) {
  /* While configured for TIM16, polling ADC register access is forbidden. */
  return state == ACQ_BUSY || mode == ACQ_MODE_TIMED;
}

acquisition_status_t acquisition_capture_one(ad4683_sample_t *sample,
                                             uint32_t timeout_ms) {
  if (sample == NULL || timeout_ms == 0U)
    return ACQUISITION_INVALID_ARGUMENT;
  if (state == ACQ_BUSY || mode == ACQ_MODE_TIMED)
    return ACQUISITION_BUSY;
  if (state != ACQ_IDLE)
    return ACQUISITION_NOT_READY;

  state = ACQ_BUSY;
  acquisition_status_t result = ACQUISITION_OK;
  uint32_t started = time_ms();
  single_start();
  for (;;) {
    uint32_t events = single_events;
    if ((events & DMA_ERROR) != 0U) {
      result = ACQUISITION_DMA_ERROR;
      break;
    }
    if (spi_has_error()) {
      result = ACQUISITION_SPI_ERROR;
      break;
    }
    if ((events & DMA_ALL_DONE) == DMA_ALL_DONE &&
        LL_SPI_IsActiveFlag_TXC(SPI5)) {
      __DMB();
      sample->a = (int16_t)single_rx_a;
      sample->b = (int16_t)single_rx_b;
      break;
    }
    if (time_expired(started, timeout_ms)) {
      result = ACQUISITION_TIMEOUT;
      break;
    }
  }
  acquisition_status_t cleanup = spi_dma_shutdown(result != ACQUISITION_OK);
  adc_cs_high();
  time_delay_us(2U);
  state = cleanup == ACQUISITION_OK ? ACQ_IDLE : ACQ_FAULT;
  return cleanup == ACQUISITION_OK ? result : cleanup;
}

static void timer_stop_outputs(void) {
  LL_TIM_DisableDMAReq_CC1(TIM16);
  LL_TIM_DisableCounter(TIM16);
  LL_TIM_DisableAllOutputs(TIM16); /* Off-state is configured high. */
}

static void timed_abort(acquisition_status_t cause) {
  if (timed_fault == ACQUISITION_OK)
    timed_fault = cause;
  timer_stop_outputs();
  state = ACQ_FAULT;
}

static void timed_rx_completed(uint32_t stream) {
  /* Disabling a DMA stream also sets TCIF (RM0433, DMA transfer suspension).
   * Only a TC from a still-enabled double-buffered stream completes a block.
   */
  if (!LL_DMA_IsEnabledStream(DMA2, stream))
    return;
  if (stream == DMA_RX_A)
    ++timed_a_completed;
  else
    ++timed_b_completed;

  /* A DMA target becomes reusable at its stream's TC, even if the other
     channel hasn't caught up yet. */
  if (timed_a_completed - timed_released > 1U ||
      timed_b_completed - timed_released > 1U) {
    timed_abort(ACQUISITION_OVERRUN);
    return;
  }

  if (timed_a_completed != timed_b_completed)
    return;
  if (LL_DMA_GetCurrentTargetMem(DMA2, DMA_RX_A) !=
      LL_DMA_GetCurrentTargetMem(DMA2, DMA_RX_B)) {
    timed_abort(ACQUISITION_SPI_ERROR);
    return;
  }
  __DMB();
  timed_pair_completed = timed_a_completed;
}

/* All three DMA IRQs use the same preemption priority (board_dma.c). */
static void dma_irq(uint32_t stream, uint32_t complete, uint32_t errors,
                    uint32_t single_done) {
  uint32_t pending = DMA2->LISR & (complete | errors);
  DMA2->LIFCR = pending;
  if (state != ACQ_BUSY)
    return;
  if (pending & errors) {
    if (mode == ACQ_MODE_TIMED)
      timed_abort(ACQUISITION_DMA_ERROR);
    else
      single_events |= DMA_ERROR;
  } else if (pending & complete) {
    if (mode == ACQ_MODE_TIMED)
      timed_rx_completed(stream);
    else
      single_events |= single_done;
  }
}

void DMA2_Stream0_IRQHandler(void) {
  dma_irq(DMA_TX, 0U, DMA_ERROR_FLAGS(0), 0U);
}

void DMA2_Stream1_IRQHandler(void) {
  dma_irq(DMA_RX_A, DMA_LISR_TCIF1, DMA_ERROR_FLAGS(1), DMA_RX_A_DONE);
}

void DMA2_Stream2_IRQHandler(void) {
  dma_irq(DMA_RX_B, DMA_LISR_TCIF2, DMA_ERROR_FLAGS(2), DMA_RX_B_DONE);
}

static void timed_config_timer(void) {
  timer_stop_outputs();
  LL_TIM_SetPrescaler(TIM16, 0U);
  LL_TIM_SetAutoReload(TIM16, timed_timer_period - 1U);
  LL_TIM_SetRepetitionCounter(TIM16, 0U);
  /* Start with CS high. At compare, CS falls and CC1 requests a TX word. */
  LL_TIM_CC_SetDMAReqTrigger(TIM16, LL_TIM_CCDMAREQUEST_CC);
  LL_TIM_OC_SetMode(TIM16, LL_TIM_CHANNEL_CH1, LL_TIM_OCMODE_PWM2);
  LL_TIM_OC_SetPolarity(TIM16, LL_TIM_CHANNEL_CH1, LL_TIM_OCPOLARITY_LOW);
  LL_TIM_OC_SetCompareCH1(TIM16, timed_timer_period - ADC_CS_LOW_TICKS);
  LL_TIM_OC_SetIdleState(TIM16, LL_TIM_CHANNEL_CH1, LL_TIM_OCIDLESTATE_HIGH);
  LL_TIM_SetOffStates(TIM16, LL_TIM_OSSI_ENABLE, LL_TIM_OSSR_ENABLE);
  LL_TIM_CC_EnableChannel(TIM16, LL_TIM_CHANNEL_CH1);
}

static void timed_reset_blocks(void) {
  timed_a_completed = 0U;
  timed_b_completed = 0U;
  timed_pair_completed = 0U;
  timed_released = 0U;
  timed_delivered = 0U;
  timed_fault = ACQUISITION_OK;
}

acquisition_status_t
acquisition_timed_init(const acquisition_timed_config_t *config) {
  if (config == NULL || config->sample_rate_hz == 0U ||
      config->sample_rate_hz > ADC_MAX_SAMPLE_RATE_HZ ||
      config->samples_per_block < 16U ||
      config->samples_per_block > ACQUISITION_MAX_BLOCK_SAMPLES ||
      (ADC_TIM_CLOCK_HZ % config->sample_rate_hz) != 0U)
    return ACQUISITION_INVALID_ARGUMENT;

  uint32_t ticks = ADC_TIM_CLOCK_HZ / config->sample_rate_hz;
  if (ticks > 65536U || ticks < ADC_CS_LOW_TICKS + ADC_CS_QUIET_TICKS ||
      SystemCoreClock != 480000000U)
    return ACQUISITION_INVALID_ARGUMENT;
  if (state == ACQ_BUSY)
    return ACQUISITION_BUSY;
  if (state != ACQ_IDLE || mode != ACQ_MODE_SINGLE)
    return ACQUISITION_NOT_READY;

  /* Configuration and polling are complete; TIM16 now owns CS and DMA2. */
  if (!spi_disable_both() || !dma_disable_all()) {
    state = ACQ_FAULT;
    return ACQUISITION_NOT_READY;
  }
  board_timer_adc_init();
  timed_timer_period = ticks;
  timed_block_samples = config->samples_per_block;
  timed_config_timer();
  adc_cs_timer();

  dma_config_tx(LL_DMAMUX1_REQ_TIM16_CH1, LL_DMA_MODE_CIRCULAR);
  dma_config_rx(DMA_RX_A, LL_DMAMUX1_REQ_SPI5_RX, SPI5, timed_rx_a[0],
                timed_rx_a[1], timed_block_samples);
  dma_config_rx(DMA_RX_B, LL_DMAMUX1_REQ_SPI4_RX, SPI4, timed_rx_b[0],
                timed_rx_b[1], timed_block_samples);
  dma_clear_pending_irqs();

  LL_SPI_SetTransferSize(SPI5, 0U); /* Endless, clock only on TXDR writes. */
  LL_SPI_SetTransferSize(SPI4, 0U);
  /* SPI5 kernel = APB2 = 120 MHz, DIV4 -> 30 MHz SCLK. */
  LL_SPI_SetBaudRatePrescaler(SPI5, LL_SPI_BAUDRATEPRESCALER_DIV4);
  spi_dma_disable_requests();
  spi_clear_flags(SPI5);
  spi_clear_flags(SPI4);

  timed_reset_blocks();
  mode = ACQ_MODE_TIMED;
  state = ACQ_IDLE;
  return ACQUISITION_OK;
}

acquisition_status_t acquisition_timed_start(void) {
  if (mode != ACQ_MODE_TIMED || state != ACQ_IDLE)
    return state == ACQ_BUSY ? ACQUISITION_BUSY : ACQUISITION_NOT_READY;

  timed_reset_blocks();
  LL_DMA_SetDataLength(DMA2, DMA_TX, 1U);
  LL_DMA_SetDataLength(DMA2, DMA_RX_A, timed_block_samples);
  LL_DMA_SetDataLength(DMA2, DMA_RX_B, timed_block_samples);
  LL_DMA_SetCurrentTargetMem(DMA2, DMA_RX_A, LL_DMA_CURRENTTARGETMEM0);
  LL_DMA_SetCurrentTargetMem(DMA2, DMA_RX_B, LL_DMA_CURRENTTARGETMEM0);
  dma_clear_pending_irqs();
  spi_clear_flags(SPI5);
  spi_clear_flags(SPI4);
  LL_TIM_GenerateEvent_UPDATE(TIM16); /* Reload and reset to the high phase. */
  LL_TIM_ClearFlag_UPDATE(TIM16);
  LL_TIM_ClearFlag_CC1(TIM16);

  /* RX DMA and SPI4 NSS slave must be armed before SPI5 starts clocking. */
  LL_DMA_EnableStream(DMA2, DMA_RX_A);
  LL_DMA_EnableStream(DMA2, DMA_RX_B);
  LL_DMA_EnableStream(DMA2, DMA_TX);
  LL_SPI_EnableDMAReq_RX(SPI5);
  LL_SPI_EnableDMAReq_RX(SPI4);
  LL_SPI_Enable(SPI4);
  LL_SPI_Enable(SPI5);
  LL_SPI_StartMasterTransfer(SPI5);

  state = ACQ_BUSY;
  LL_TIM_EnableDMAReq_CC1(TIM16);
  LL_TIM_EnableAllOutputs(TIM16);
  LL_TIM_EnableCounter(TIM16);
  return ACQUISITION_OK;
}

static bool timed_block_valid(uint32_t sequence) {
  uint32_t expected =
      (sequence & 1U) ? LL_DMA_CURRENTTARGETMEM1 : LL_DMA_CURRENTTARGETMEM0;
  return LL_DMA_GetCurrentTargetMem(DMA2, DMA_RX_A) == expected &&
         LL_DMA_GetCurrentTargetMem(DMA2, DMA_RX_B) == expected;
}

acquisition_status_t acquisition_timed_get_block(acquisition_block_t *block) {
  if (block == NULL)
    return ACQUISITION_INVALID_ARGUMENT;
  if (mode != ACQ_MODE_TIMED || state == ACQ_UNINITIALIZED)
    return ACQUISITION_NOT_READY;
  if (timed_fault != ACQUISITION_OK)
    return timed_fault;
  if (state == ACQ_BUSY && spi_has_error()) {
    timed_abort(ACQUISITION_SPI_ERROR);
    return ACQUISITION_SPI_ERROR;
  }
  uint32_t ready = timed_pair_completed;
  if (ready == timed_delivered || !timed_block_valid(ready))
    return ACQUISITION_BUSY;
  uint32_t index = (ready - 1U) & 1U;
  if (timed_fault != ACQUISITION_OK)
    return timed_fault;
  __DMB();
  block->a = timed_rx_a[index];
  block->b = timed_rx_b[index];
  block->count = timed_block_samples;
  block->sequence = ready;
  timed_delivered = ready;
  return ACQUISITION_OK;
}

acquisition_status_t acquisition_timed_release_block(uint32_t sequence) {
  if (mode != ACQ_MODE_TIMED || sequence != timed_delivered ||
      sequence != timed_released + 1U)
    return ACQUISITION_INVALID_ARGUMENT;
  if (timed_fault != ACQUISITION_OK)
    return timed_fault;
  /* The caller has finished reading both channels from this memory target. */
  __DMB();
  if (!timed_block_valid(sequence)) {
    timed_abort(ACQUISITION_OVERRUN);
    return ACQUISITION_OVERRUN;
  }
  timed_released = sequence;
  return ACQUISITION_OK;
}

static acquisition_status_t timed_shutdown(void) {
  /* Disable the source first. A final partial SPI frame may be discarded;
   * only previously completed pairs are exposed to the application.
   */
  timer_stop_outputs();
  state = ACQ_IDLE; /* Prevent DMA shutdown flags from publishing a block. */
  acquisition_status_t cleanup = spi_dma_shutdown(true);
  if (cleanup != ACQUISITION_OK) {
    state = ACQ_FAULT;
    timed_fault = cleanup;
  } else if (timed_fault == ACQUISITION_OK &&
             timed_pair_completed != timed_released &&
             !timed_block_valid(timed_pair_completed)) {
    timed_fault = ACQUISITION_OVERRUN;
  }
  return cleanup;
}

acquisition_status_t acquisition_timed_stop(void) {
  if (mode != ACQ_MODE_TIMED)
    return ACQUISITION_NOT_READY;

  acquisition_status_t cleanup = timed_shutdown();
  return cleanup == ACQUISITION_OK ? timed_fault : cleanup;
}

acquisition_status_t acquisition_timed_deinit(void) {
  if (mode != ACQ_MODE_TIMED)
    return ACQUISITION_NOT_READY;
  acquisition_status_t cleanup = timed_shutdown();
  adc_cs_gpio();
  time_delay_us(2U);
  if (cleanup != ACQUISITION_OK)
    return cleanup;
  LL_SPI_SetBaudRatePrescaler(SPI5, (uint32_t)adc_master_spi_config.baud_divider
                                        << SPI_CFG1_MBR_Pos);
  dma_config_single();
  dma_clear_pending_irqs();
  mode = ACQ_MODE_SINGLE;
  state = ACQ_IDLE;
  return ACQUISITION_OK;
}
