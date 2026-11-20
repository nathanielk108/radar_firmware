#include "services/acquisition.h"

#include "board/board_dma.h"
#include "board/board_pins.h"
#include "board/board_spi.h"
#include "board/board_timer.h"
#include "platform/clock.h"
#include "platform/spi.h"
#include "platform/time.h"

#include "stm32h7xx_ll_dma.h"
#include "stm32h7xx_ll_gpio.h"
#include "stm32h7xx_ll_spi.h"
#include "stm32h7xx_ll_tim.h"

#include <stddef.h>

/* These clocks are defined by platform/clock.c. Update both together. */
#define ADC_CS_LOW_TICKS (ACQUISITION_TIMER_TICKS - ACQUISITION_CS_OFFSET_TICKS)
#define ADC_CS_QUIET_TICKS 360U /* AD4683 tQUIET = 1.5 us */
#define ADC_TIMER_TICKS_PER_US (ACQUISITION_TIMER_HZ / 1000000U)
#define ADC_STOP_TIMEOUT_MS 2U
#define ADC_BURST_TIMEOUT_MS 5U
#define DMA_BUFFER __attribute__((section(".dma_buffer"), aligned(32)))

#define DMA_TX LL_DMA_STREAM_0
#define DMA_RX_A LL_DMA_STREAM_1
#define DMA_RX_B LL_DMA_STREAM_2

#define DMA_ERROR_FLAGS(n)                                                     \
  (DMA_LISR_TEIF##n | DMA_LISR_DMEIF##n | DMA_LISR_FEIF##n)
#define DMA_STREAM_FLAGS(n)                                                    \
  (DMA_ERROR_FLAGS(n) | DMA_LISR_TCIF##n | DMA_LISR_HTIF##n)

_Static_assert(ACQUISITION_TIMER_HZ == PLATFORM_APB_TIMER_HZ,
               "TIM16 timing must match the platform timer clock");

/* .dma_buffer is NOLOAD. Initialize TX and publish RX only after DMA fills it.
 */
DMA_BUFFER static uint16_t adc_dummy_tx;
DMA_BUFFER static int16_t timed_rx_a[2][ACQUISITION_MAX_BLOCK_SAMPLES];
DMA_BUFFER static int16_t timed_rx_b[2][ACQUISITION_MAX_BLOCK_SAMPLES];

/* DMA starts one word before the aligned view to discard the old conversion. */
#define BURST_ALIGNMENT_WORDS (32U / sizeof(int16_t))
#define BURST_FRAMES_ALIGNED(samples)                                           \
  (((samples) + BURST_ALIGNMENT_WORDS) & ~(BURST_ALIGNMENT_WORDS - 1U))
#define BURST_BUFFER_WORDS                                                     \
  (BURST_ALIGNMENT_WORDS - 1U +                                                  \
   ACQUISITION_MAX_BURST_COUNT *                                                 \
       BURST_FRAMES_ALIGNED(ACQUISITION_MAX_BLOCK_SAMPLES))
_Static_assert(BURST_FRAMES_ALIGNED(ACQUISITION_MAX_BLOCK_SAMPLES) <= 256U,
               "TIM16 must fit the padded burst length");
_Static_assert(BURST_BUFFER_WORDS <= UINT16_MAX,
               "RX DMA must fit the entire padded train");
DMA_BUFFER static int16_t burst_rx_a[BURST_BUFFER_WORDS];
DMA_BUFFER static int16_t burst_rx_b[BURST_BUFFER_WORDS];

typedef enum { ACQ_UNINITIALIZED, ACQ_IDLE, ACQ_BUSY, ACQ_FAULT } acq_state_t;

typedef enum { ACQ_MODE_NONE, ACQ_MODE_TIMED, ACQ_MODE_BURST } acq_mode_t;

static volatile acq_state_t state = ACQ_UNINITIALIZED;
static volatile acq_mode_t mode;
static volatile uint32_t timed_a_completed;
static volatile uint32_t timed_b_completed;
static volatile uint32_t timed_pair_completed;
static volatile acquisition_status_t capture_fault;
static volatile uint32_t timed_released;
static bool timed_primed;
static uint32_t timed_delivered;
static uint32_t timed_sequence;
static uint32_t timed_period_ticks;
static uint32_t timed_start_us;
static uint64_t timed_first_sample_ticks;
static uint16_t capture_samples;

static void adc_cs_high(void) {
  LL_GPIO_SetOutputPin(ADC_A_CS_PORT, 1UL << ADC_A_CS_PIN);
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

static void dma_config_tx(void) {
  LL_DMA_ConfigTransfer(
      DMA2, DMA_TX,
      LL_DMA_DIRECTION_MEMORY_TO_PERIPH | LL_DMA_PRIORITY_HIGH |
          LL_DMA_MODE_CIRCULAR | LL_DMA_PERIPH_NOINCREMENT |
          LL_DMA_MEMORY_NOINCREMENT | LL_DMA_PDATAALIGN_HALFWORD |
          LL_DMA_MDATAALIGN_HALFWORD);
  LL_DMA_ConfigAddresses(DMA2, DMA_TX, (uint32_t)(uintptr_t)&adc_dummy_tx,
                         (uint32_t)(uintptr_t)&SPI5->TXDR,
                         LL_DMA_DIRECTION_MEMORY_TO_PERIPH);
  LL_DMA_SetPeriphRequest(DMA2, DMA_TX, LL_DMAMUX1_REQ_TIM16_CH1);
  LL_DMA_SetDataLength(DMA2, DMA_TX, 1U);

  /* Report errors only. A timer event clocks each SPI frame. */
  LL_DMA_DisableIT_TC(DMA2, DMA_TX);
  LL_DMA_EnableIT_TE(DMA2, DMA_TX);
  LL_DMA_EnableIT_DME(DMA2, DMA_TX);
  LL_DMA_EnableIT_FE(DMA2, DMA_TX);
}

static void dma_config_rx(uint32_t stream, uint32_t request, SPI_TypeDef *spi,
                          int16_t *memory0, int16_t *memory1, uint16_t count) {
  uint32_t flags = LL_DMA_DIRECTION_PERIPH_TO_MEMORY |
                   LL_DMA_PRIORITY_VERYHIGH | LL_DMA_PERIPH_NOINCREMENT |
                   LL_DMA_PDATAALIGN_HALFWORD | LL_DMA_MDATAALIGN_HALFWORD |
                   LL_DMA_MEMORY_INCREMENT;
  if (memory1) {
    flags |= LL_DMA_MODE_CIRCULAR | LL_DMA_DOUBLEBUFFER_MODE_ENABLE |
             LL_DMA_CURRENTTARGETMEM0;
  }
  LL_DMA_ConfigTransfer(DMA2, stream, flags);
  LL_DMA_ConfigAddresses(DMA2, stream, (uint32_t)(uintptr_t)&spi->RXDR,
                         (uint32_t)(uintptr_t)memory0,
                         LL_DMA_DIRECTION_PERIPH_TO_MEMORY);
  if (memory1)
    LL_DMA_SetMemory1Address(DMA2, stream, (uint32_t)(uintptr_t)memory1);
  LL_DMA_SetDataLength(DMA2, stream, count);
  LL_DMA_SetPeriphRequest(DMA2, stream, request);

  if (memory1)
    LL_DMA_EnableIT_TC(DMA2, stream);
  else
    LL_DMA_DisableIT_TC(DMA2, stream);
  LL_DMA_EnableIT_TE(DMA2, stream);
  LL_DMA_EnableIT_DME(DMA2, stream);
  LL_DMA_EnableIT_FE(DMA2, stream);
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

static acquisition_status_t spi_dma_shutdown(void) {
  bool master_stopped = spi_suspend_master();
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
  if (mode != ACQ_MODE_NONE)
    return ACQUISITION_BUSY;
  if (state == ACQ_IDLE)
    return ACQUISITION_OK;

  board_dma_adc_init();
  board_timer_adc_init();
  adc_cs_gpio();
  if (spi_dma_shutdown() != ACQUISITION_OK) {
    state = ACQ_FAULT;
    return ACQUISITION_NOT_READY;
  }
  adc_dummy_tx = 0U;
  __DMB();
  board_dma_adc_enable_irqs();
  state = ACQ_IDLE;
  return ACQUISITION_OK;
}

acquisition_status_t acquisition_deinit(void) {
  if (mode != ACQ_MODE_NONE)
    return ACQUISITION_BUSY;
  state = ACQ_UNINITIALIZED;
  return ACQUISITION_OK;
}

bool acquisition_active(void) { return mode != ACQ_MODE_NONE; }

static void timer_stop_outputs(void) {
  LL_TIM_DisableDMAReq_CC1(TIM16);
  LL_TIM_DisableCounter(TIM16);
  LL_TIM_DisableAllOutputs(TIM16); /* Off-state is configured high. */
}

static void capture_abort(acquisition_status_t cause) {
  if (capture_fault == ACQUISITION_OK)
    capture_fault = cause;
  timer_stop_outputs();
  state = ACQ_FAULT;
}

static bool timed_block_valid(uint32_t completed) {
  uint32_t expected =
      (completed & 1U) ? LL_DMA_CURRENTTARGETMEM1 : LL_DMA_CURRENTTARGETMEM0;
  return LL_DMA_GetCurrentTargetMem(DMA2, DMA_RX_A) == expected &&
         LL_DMA_GetCurrentTargetMem(DMA2, DMA_RX_B) == expected;
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
    capture_abort(ACQUISITION_OVERRUN);
    return;
  }

  if (timed_a_completed != timed_b_completed)
    return;
  if (!timed_block_valid(timed_a_completed)) {
    capture_abort(ACQUISITION_OVERRUN);
    return;
  }
  /* Hide the initial stale conversion without shifting either DMA view or
   * shortening the first delivered block. */
  if (!timed_primed) {
    timed_primed = true;
    timed_released = 1U;
    return;
  }
  __DMB();
  timed_pair_completed = timed_a_completed;
}

/* All three DMA IRQs use the same preemption priority (board_dma.c). */
static void dma_irq(uint32_t stream, uint32_t complete, uint32_t errors) {
  uint32_t pending = DMA2->LISR & (complete | errors);
  DMA2->LIFCR = pending;
  if (state != ACQ_BUSY)
    return;
  if (pending & errors)
    capture_abort(ACQUISITION_DMA_ERROR);
  else if (mode == ACQ_MODE_TIMED && (pending & complete))
    timed_rx_completed(stream);
}

void DMA2_Stream0_IRQHandler(void) { dma_irq(DMA_TX, 0U, DMA_ERROR_FLAGS(0)); }

void DMA2_Stream1_IRQHandler(void) {
  dma_irq(DMA_RX_A, DMA_LISR_TCIF1, DMA_ERROR_FLAGS(1));
}

void DMA2_Stream2_IRQHandler(void) {
  dma_irq(DMA_RX_B, DMA_LISR_TCIF2, DMA_ERROR_FLAGS(2));
}

static void configure_timer(uint32_t period, uint16_t frames) {
  timer_stop_outputs();
  LL_TIM_SetPrescaler(TIM16, 0U);
  LL_TIM_SetAutoReload(TIM16, period - 1U);
  LL_TIM_SetOnePulseMode(TIM16, frames ? LL_TIM_ONEPULSEMODE_SINGLE
                                       : LL_TIM_ONEPULSEMODE_REPETITIVE);
  LL_TIM_SetRepetitionCounter(TIM16, frames ? frames - 1U : 0U);
  /* Start with CS high. At compare, CS falls and CC1 requests a TX word. */
  LL_TIM_CC_SetDMAReqTrigger(TIM16, LL_TIM_CCDMAREQUEST_CC);
  LL_TIM_OC_SetMode(TIM16, LL_TIM_CHANNEL_CH1, LL_TIM_OCMODE_PWM2);
  LL_TIM_OC_SetPolarity(TIM16, LL_TIM_CHANNEL_CH1, LL_TIM_OCPOLARITY_LOW);
  LL_TIM_OC_SetCompareCH1(TIM16, period - ADC_CS_LOW_TICKS);
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
  timed_primed = false;
  capture_fault = ACQUISITION_OK;
}

static void configure_capture(uint32_t period, uint16_t frames) {
  configure_timer(period, frames);
  adc_cs_timer();
  dma_config_tx();
  LL_SPI_SetTransferSize(SPI5, 0U);
  LL_SPI_SetTransferSize(SPI4, 0U);
  /* 120 MHz SPI5 kernel / 4 = 30 MHz. */
  LL_SPI_SetBaudRatePrescaler(SPI5, LL_SPI_BAUDRATEPRESCALER_DIV4);
}

static bool arm_capture(uint16_t dma_words) {
  LL_DMA_SetDataLength(DMA2, DMA_TX, 1U);
  LL_DMA_SetDataLength(DMA2, DMA_RX_A, dma_words);
  LL_DMA_SetDataLength(DMA2, DMA_RX_B, dma_words);
  dma_clear_pending_irqs();
  spi_clear_flags(SPI5);
  spi_clear_flags(SPI4);
  /* Load PSC and RCR, then clear the artificial update before arming. */
  LL_TIM_ClearFlag_UPDATE(TIM16);
  LL_TIM_GenerateEvent_UPDATE(TIM16);
  uint32_t started = time_ms();
  while (!LL_TIM_IsActiveFlag_UPDATE(TIM16)) {
    if (time_expired(started, ADC_STOP_TIMEOUT_MS))
      return false;
  }
  LL_TIM_ClearFlag_UPDATE(TIM16);
  LL_TIM_ClearFlag_CC1(TIM16);

  state = ACQ_BUSY;
  LL_DMA_EnableStream(DMA2, DMA_RX_A);
  LL_DMA_EnableStream(DMA2, DMA_RX_B);
  LL_DMA_EnableStream(DMA2, DMA_TX);
  LL_SPI_EnableDMAReq_RX(SPI5);
  LL_SPI_EnableDMAReq_RX(SPI4);
  LL_SPI_Enable(SPI4);
  LL_SPI_Enable(SPI5);
  LL_SPI_StartMasterTransfer(SPI5);
  LL_TIM_EnableDMAReq_CC1(TIM16);
  LL_TIM_EnableAllOutputs(TIM16);
  return true;
}

static uint32_t start_adc_timer(void) {
  uint32_t irq_state = __get_PRIMASK();
  __disable_irq();
  uint32_t started_us = time_us();
  LL_TIM_EnableCounter(TIM16);
  __set_PRIMASK(irq_state);
  return started_us;
}

acquisition_status_t
acquisition_timed_init(const acquisition_timed_config_t *config) {
  if (!config || !config->sample_rate_hz ||
      config->sample_rate_hz > ACQUISITION_SAMPLE_RATE_HZ ||
      config->samples_per_block < 16U ||
      config->samples_per_block > ACQUISITION_MAX_BLOCK_SAMPLES ||
      ACQUISITION_TIMER_HZ % config->sample_rate_hz != 0U)
    return ACQUISITION_INVALID_ARGUMENT;

  uint32_t ticks = ACQUISITION_TIMER_HZ / config->sample_rate_hz;
  if (ticks > 65536U || ticks < ADC_CS_LOW_TICKS + ADC_CS_QUIET_TICKS ||
      SystemCoreClock != 480000000U)
    return ACQUISITION_INVALID_ARGUMENT;
  if (state == ACQ_BUSY)
    return ACQUISITION_BUSY;
  if (state != ACQ_IDLE || mode != ACQ_MODE_NONE)
    return ACQUISITION_NOT_READY;

  capture_samples = config->samples_per_block;
  timed_period_ticks = ticks;
  configure_capture(ticks, 0U);
  dma_config_rx(DMA_RX_A, LL_DMAMUX1_REQ_SPI5_RX, SPI5, timed_rx_a[0],
                timed_rx_a[1], capture_samples);
  dma_config_rx(DMA_RX_B, LL_DMAMUX1_REQ_SPI4_RX, SPI4, timed_rx_b[0],
                timed_rx_b[1], capture_samples);
  timed_reset_blocks();
  mode = ACQ_MODE_TIMED;
  return ACQUISITION_OK;
}

acquisition_status_t acquisition_timed_start(void) {
  if (mode != ACQ_MODE_TIMED || state != ACQ_IDLE)
    return state == ACQ_BUSY ? ACQUISITION_BUSY : ACQUISITION_NOT_READY;

  timed_reset_blocks();
  LL_DMA_SetCurrentTargetMem(DMA2, DMA_RX_A, LL_DMA_CURRENTTARGETMEM0);
  LL_DMA_SetCurrentTargetMem(DMA2, DMA_RX_B, LL_DMA_CURRENTTARGETMEM0);
  if (!arm_capture(capture_samples))
    return ACQUISITION_TIMEOUT;
  /* The second DMA block starts with the last conversion from the first
   * block. Its CS edge is one low pulse width before the block boundary. */
  timed_first_sample_ticks =
      (uint64_t)capture_samples * timed_period_ticks - ADC_CS_LOW_TICKS;
  timed_start_us = start_adc_timer();
  return ACQUISITION_OK;
}

static acquisition_status_t check_capture_fault(void) {
  if (state == ACQ_BUSY && spi_has_error())
    capture_abort(ACQUISITION_SPI_ERROR);
  return capture_fault;
}

acquisition_status_t acquisition_timed_get_block(acquisition_block_t *block) {
  if (block == NULL)
    return ACQUISITION_INVALID_ARGUMENT;
  if (mode != ACQ_MODE_TIMED || state == ACQ_UNINITIALIZED)
    return ACQUISITION_NOT_READY;
  uint32_t irq_state = __get_PRIMASK();
  __disable_irq();
  acquisition_status_t status = check_capture_fault();
  uint32_t completed = timed_pair_completed;
  if (status == ACQUISITION_OK) {
    if (completed == timed_delivered) {
      status = ACQUISITION_BUSY;
    } else if (!timed_block_valid(completed)) {
      status = ACQUISITION_OVERRUN;
      capture_abort(status);
    } else {
      timed_delivered = completed;
    }
  }
  __set_PRIMASK(irq_state);
  if (status != ACQUISITION_OK)
    return status;

  __DMB();
  uint32_t index = (completed - 1U) & 1U;
  *block = (acquisition_block_t){
      .a = timed_rx_a[index],
      .b = timed_rx_b[index],
      .sequence = ++timed_sequence,
      .first_sample_us =
          timed_start_us +
          (uint32_t)(timed_first_sample_ticks / ADC_TIMER_TICKS_PER_US),
      .sample_rate_hz = ACQUISITION_TIMER_HZ / timed_period_ticks,
      .count = capture_samples,
      .timestamp_valid = true,
  };
  timed_first_sample_ticks += (uint64_t)capture_samples * timed_period_ticks;
  return ACQUISITION_OK;
}

acquisition_status_t acquisition_timed_release_block(uint32_t sequence) {
  if (mode != ACQ_MODE_TIMED || sequence != timed_sequence ||
      timed_delivered != timed_released + 1U)
    return ACQUISITION_INVALID_ARGUMENT;
  /* Finish all reads before giving the DMA target back. Keep the target check
   * and release atomic with respect to the completion IRQs. */
  __DMB();
  uint32_t irq_state = __get_PRIMASK();
  __disable_irq();
  acquisition_status_t status = check_capture_fault();
  if (status == ACQUISITION_OK) {
    if (!timed_block_valid(timed_delivered)) {
      status = ACQUISITION_OVERRUN;
      capture_abort(status);
    } else {
      timed_released = timed_delivered;
    }
  }
  __set_PRIMASK(irq_state);
  return status;
}

static acquisition_status_t timed_shutdown(void) {
  /* Disable the source first. A final partial SPI frame may be discarded;
   * only previously completed pairs are exposed to the application.
   */
  uint32_t irq_state = __get_PRIMASK();
  __disable_irq();
  timer_stop_outputs();
  (void)check_capture_fault();
  state = ACQ_IDLE; /* Prevent DMA shutdown flags from publishing a block. */
  __set_PRIMASK(irq_state);
  acquisition_status_t cleanup = spi_dma_shutdown();
  if (cleanup != ACQUISITION_OK) {
    state = ACQ_FAULT;
    capture_fault = cleanup;
  } else if (mode == ACQ_MODE_TIMED && capture_fault == ACQUISITION_OK &&
             timed_pair_completed - timed_released == 1U &&
             !timed_block_valid(timed_pair_completed)) {
    capture_fault = ACQUISITION_OVERRUN;
  }
  return cleanup;
}

acquisition_status_t acquisition_timed_stop(void) {
  if (mode != ACQ_MODE_TIMED)
    return ACQUISITION_NOT_READY;

  acquisition_status_t cleanup = timed_shutdown();
  return cleanup == ACQUISITION_OK ? capture_fault : cleanup;
}

static acquisition_status_t restore_polling(void) {
  acquisition_status_t cleanup = timed_shutdown();
  adc_cs_gpio();
  time_delay_us(2U);
  /* Release ownership even on failure. ACQ_FAULT blocks new captures until
   * acquisition_init() successfully recovers the peripherals. */
  mode = ACQ_MODE_NONE;
  if (cleanup != ACQUISITION_OK)
    return cleanup;
  LL_SPI_SetBaudRatePrescaler(SPI5, (uint32_t)adc_master_spi_config.baud_divider
                                        << SPI_CFG1_MBR_Pos);
  return ACQUISITION_OK;
}

acquisition_status_t acquisition_timed_deinit(void) {
  return mode == ACQ_MODE_TIMED ? restore_polling() : ACQUISITION_NOT_READY;
}

static acquisition_status_t arm_bursts(uint16_t samples, uint16_t bursts,
                                       uint16_t frames) {
  if (samples == 0U || samples > ACQUISITION_MAX_BLOCK_SAMPLES ||
      bursts == 0U || bursts > ACQUISITION_MAX_BURST_COUNT ||
      SystemCoreClock != 480000000U)
    return ACQUISITION_INVALID_ARGUMENT;
  if (state == ACQ_BUSY)
    return ACQUISITION_BUSY;
  if (state != ACQ_IDLE || mode != ACQ_MODE_NONE)
    return ACQUISITION_NOT_READY;

  /* Frame 0 returns the old sample; frame N reads back conversion N-1. */
  uint16_t words = frames * bursts;
  configure_capture(ACQUISITION_TIMER_TICKS, frames);
  dma_config_rx(DMA_RX_A, LL_DMAMUX1_REQ_SPI5_RX, SPI5,
                &burst_rx_a[BURST_ALIGNMENT_WORDS - 1U], NULL, words);
  dma_config_rx(DMA_RX_B, LL_DMAMUX1_REQ_SPI4_RX, SPI4,
                &burst_rx_b[BURST_ALIGNMENT_WORDS - 1U], NULL, words);
  capture_samples = samples;
  capture_fault = ACQUISITION_OK;
  mode = ACQ_MODE_BURST;
  if (!arm_capture(words)) {
    acquisition_status_t cleanup = restore_polling();
    return cleanup == ACQUISITION_OK ? ACQUISITION_TIMEOUT : cleanup;
  }
  return ACQUISITION_OK;
}

acquisition_status_t acquisition_burst_arm(uint16_t samples) {
  return arm_bursts(samples, 1U, samples + 1U);
}

acquisition_status_t acquisition_burst_train_arm(uint16_t samples,
                                                uint16_t bursts,
                                                uint16_t *stride) {
  if (!stride || bursts < 2U)
    return ACQUISITION_INVALID_ARGUMENT;
  uint16_t frames = BURST_FRAMES_ALIGNED(samples);
  acquisition_status_t status = arm_bursts(samples, bursts, frames);
  if (status == ACQUISITION_OK)
    *stride = frames;
  return status;
}

acquisition_status_t acquisition_burst_finish(acquisition_block_t *block) {
  if (!block)
    return ACQUISITION_INVALID_ARGUMENT;
  if (mode != ACQ_MODE_BURST)
    return ACQUISITION_NOT_READY;
  if (capture_fault != ACQUISITION_OK)
    return capture_fault;
  if (spi_has_error())
    return ACQUISITION_SPI_ERROR;
  if (LL_TIM_IsEnabledCounter(TIM16) || !LL_TIM_IsActiveFlag_UPDATE(TIM16))
    return ACQUISITION_BUSY;
  if (LL_DMA_GetDataLength(DMA2, DMA_RX_A) != 0U ||
      LL_DMA_GetDataLength(DMA2, DMA_RX_B) != 0U ||
      !LL_SPI_IsActiveFlag_TXC(SPI5))
    return ACQUISITION_BUSY;

  acquisition_status_t status = acquisition_burst_stop();
  if (status != ACQUISITION_OK)
    return status;
  __DMB();
  *block = (acquisition_block_t){.a = &burst_rx_a[BURST_ALIGNMENT_WORDS],
                                 .b = &burst_rx_b[BURST_ALIGNMENT_WORDS],
                                 .sample_rate_hz = ACQUISITION_SAMPLE_RATE_HZ,
                                 .count = capture_samples};
  return ACQUISITION_OK;
}

acquisition_status_t acquisition_burst_stop(void) {
  if (mode != ACQ_MODE_BURST)
    return ACQUISITION_NOT_READY;
  acquisition_status_t cleanup = restore_polling();
  return cleanup == ACQUISITION_OK ? capture_fault : cleanup;
}

acquisition_status_t acquisition_capture_block(uint16_t samples,
                                              acquisition_block_t *block) {
  if (!block)
    return ACQUISITION_INVALID_ARGUMENT;
  acquisition_status_t status = acquisition_burst_arm(samples);
  if (status != ACQUISITION_OK)
    return status;

  uint32_t started = time_ms();
  uint32_t first_sample_us =
      start_adc_timer() + ACQUISITION_CS_OFFSET_TICKS / ADC_TIMER_TICKS_PER_US;
  do {
    status = acquisition_burst_finish(block);
  } while (status == ACQUISITION_BUSY &&
           !time_expired(started, ADC_BURST_TIMEOUT_MS));
  if (status != ACQUISITION_OK && mode == ACQ_MODE_BURST) {
    acquisition_status_t cleanup = acquisition_burst_stop();
    if (cleanup != ACQUISITION_OK)
      return cleanup;
  }
  if (status == ACQUISITION_OK) {
    block->first_sample_us = first_sample_us;
    block->timestamp_valid = true;
  }
  return status == ACQUISITION_BUSY ? ACQUISITION_TIMEOUT : status;
}
