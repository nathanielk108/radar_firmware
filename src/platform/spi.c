#include "platform/spi.h"
#include "platform/time.h"

#include "stm32h7xx_ll_bus.h"
#include "stm32h7xx_ll_gpio.h"
#include "stm32h7xx_ll_spi.h"

/* Both drivers are polling-only; DMA will replace this path for acquisition. */
static bool enable_clock(SPI_TypeDef *spi) {
  if (spi == SPI1)
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SPI1);
  else if (spi == SPI2)
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_SPI2);
  else if (spi == SPI3)
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_SPI3);
  else if (spi == SPI4)
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SPI4);
  else if (spi == SPI5)
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SPI5);
  else if (spi == SPI6)
    LL_APB4_GRP1_EnableClock(LL_APB4_GRP1_PERIPH_SPI6);
  else
    return false;
  return true;
}

static bool valid_config(const spi_config_t *cfg) {
  return cfg && cfg->spi && cfg->data_size >= 4U && cfg->data_size <= 32U &&
         cfg->role <= SPI_ROLE_MASTER &&
         cfg->communication <= SPI_COMM_HALF_DUPLEX &&
         cfg->mode <= SPI_MODE_3 && cfg->bit_order <= SPI_LSB_FIRST &&
         cfg->baud_divider <= SPI_DIV_256;
}

static void write_frame(SPI_TypeDef *spi, uint32_t value, uint8_t bits) {
  if (bits <= 8U)
    *(volatile uint8_t *)&spi->TXDR = (uint8_t)value;
  else if (bits <= 16U)
    *(volatile uint16_t *)&spi->TXDR = (uint16_t)value;
  else
    spi->TXDR = value;
}

static uint32_t read_frame(SPI_TypeDef *spi, uint8_t bits) {
  if (bits <= 8U)
    return *(volatile uint8_t *)&spi->RXDR;
  if (bits <= 16U)
    return *(volatile uint16_t *)&spi->RXDR;
  return spi->RXDR;
}

static uint32_t tx_frame(const void *buffer, uint16_t index, uint8_t bits) {
  if (!buffer)
    return 0U;
  if (bits <= 8U)
    return ((const uint8_t *)buffer)[index];
  if (bits <= 16U)
    return ((const uint16_t *)buffer)[index];
  return ((const uint32_t *)buffer)[index];
}

static void rx_frame(void *buffer, uint16_t index, uint8_t bits,
                     uint32_t value) {
  if (!buffer)
    return;
  if (bits <= 8U)
    ((uint8_t *)buffer)[index] = (uint8_t)value;
  else if (bits <= 16U)
    ((uint16_t *)buffer)[index] = (uint16_t)value;
  else
    ((uint32_t *)buffer)[index] = value;
}

static void clear_flags(SPI_TypeDef *spi) {
  spi->IFCR = SPI_IFCR_EOTC | SPI_IFCR_TXTFC | SPI_IFCR_OVRC | SPI_IFCR_MODFC |
              SPI_IFCR_SUSPC;
}

/* One failure does not leave SPE permanently cleared for the next transaction.
 */
static void abort_transfer(SPI_TypeDef *spi) {
  if ((spi->CR1 & SPI_CR1_CSTART) != 0U) {
    LL_SPI_SuspendMasterTransfer(spi);
    uint32_t start = time_ms();
    while (((spi->SR & (SPI_SR_SUSP | SPI_SR_EOT)) == 0U) &&
           !time_expired(start, 2U)) {
    }
  }
  LL_SPI_Disable(spi);
  clear_flags(spi);
}

spi_status_t spi_init(const spi_config_t *cfg) {
  if (!valid_config(cfg) || !enable_clock(cfg->spi))
    return SPI_STATUS_INVALID_ARGUMENT;

  SPI_TypeDef *spi = cfg->spi;
  LL_SPI_Disable(spi);
  spi->CR1 = 0U;
  spi->CR2 = 0U;
  spi->CFG1 = 0U;
  spi->CFG2 = 0U;

  LL_SPI_SetDataWidth(spi, (uint32_t)cfg->data_size - 1U);
  LL_SPI_SetFIFOThreshold(spi, LL_SPI_FIFO_TH_01DATA);
  LL_SPI_SetBaudRatePrescaler(spi,
                              (uint32_t)cfg->baud_divider << SPI_CFG1_MBR_Pos);
  LL_SPI_SetNSSMode(spi, cfg->software_nss ? LL_SPI_NSS_SOFT
                                           : LL_SPI_NSS_HARD_INPUT);
  if (cfg->software_nss)
    SET_BIT(spi->CR1, SPI_CR1_SSI);
  LL_SPI_SetMode(spi, cfg->role == SPI_ROLE_MASTER ? LL_SPI_MODE_MASTER
                                                   : LL_SPI_MODE_SLAVE);
  LL_SPI_SetTransferDirection(spi, (uint32_t)cfg->communication
                                       << SPI_CFG2_COMM_Pos);
  LL_SPI_SetClockPolarity(spi, cfg->mode >= SPI_MODE_2 ? LL_SPI_POLARITY_HIGH
                                                       : LL_SPI_POLARITY_LOW);
  LL_SPI_SetClockPhase(spi, (cfg->mode & 1U) ? LL_SPI_PHASE_2EDGE
                                             : LL_SPI_PHASE_1EDGE);
  LL_SPI_SetTransferBitOrder(spi, cfg->bit_order == SPI_LSB_FIRST
                                      ? LL_SPI_LSB_FIRST
                                      : LL_SPI_MSB_FIRST);
  clear_flags(spi);
  if (spi == SPI1) {
    LL_SPI_EnableIOSwap(spi);
  }
  LL_SPI_Enable(spi);
  return SPI_STATUS_OK;
}

spi_status_t spi_transfer(const spi_config_t *cfg, const void *tx, void *rx,
                          uint16_t count, uint32_t timeout_ms) {
  if (!valid_config(cfg) || cfg->role != SPI_ROLE_MASTER ||
      cfg->communication != SPI_COMM_FULL_DUPLEX || count == 0U ||
      timeout_ms == 0U)
    return SPI_STATUS_INVALID_ARGUMENT;

  SPI_TypeDef *spi = cfg->spi;
  uint32_t start = time_ms();
  uint16_t written = 0U;
  uint16_t received = 0U;

  /* Reset and configure transaction while SPI is disabled. */
  LL_SPI_Disable(spi);
  LL_SPI_SetTransferSize(spi, count);
  clear_flags(spi);
  LL_SPI_Enable(spi);

  /* The first frame must be in TXDR BEFORE CSTART. */
  while ((spi->SR & SPI_SR_TXP) == 0U) {
    if (time_expired(start, timeout_ms)) {
      abort_transfer(spi);
      return SPI_STATUS_TIMEOUT;
    }
  }
  write_frame(spi, tx_frame(tx, written++, cfg->data_size), cfg->data_size);
  LL_SPI_StartMasterTransfer(spi);

  for (;;) {
    uint32_t sr = spi->SR;
    if (sr & SPI_SR_OVR) {
      abort_transfer(spi);
      return SPI_STATUS_OVERRUN;
    }
    if (sr & SPI_SR_MODF) {
      abort_transfer(spi);
      return SPI_STATUS_MODE_FAULT;
    }

    /* Drain RX before feeding TX to avoid overrun on small FIFO depths. */
    if ((sr & SPI_SR_RXP) && received < count) {
      rx_frame(rx, received++, cfg->data_size, read_frame(spi, cfg->data_size));
    }
    if ((sr & SPI_SR_TXP) && written < count) {
      write_frame(spi, tx_frame(tx, written++, cfg->data_size), cfg->data_size);
    }
    if (received == count && (spi->SR & SPI_SR_EOT))
      break;
    if (time_expired(start, timeout_ms)) {
      abort_transfer(spi);
      return SPI_STATUS_TIMEOUT;
    }
  }

  LL_SPI_Disable(spi);
  clear_flags(spi);
  LL_SPI_Enable(spi);
  return SPI_STATUS_OK;
}

spi_status_t spi_transfer_pair(const spi_config_t *master,
                               const spi_config_t *slave, uint16_t tx,
                               uint16_t *a, uint16_t *b, GPIO_TypeDef *cs_port,
                               uint8_t cs_pin, uint32_t timeout_ms) {
  if (!valid_config(master) || !valid_config(slave) || !a || !b || !cs_port ||
      cs_pin > 15U || !timeout_ms || master->spi != SPI5 ||
      slave->spi != SPI4 || master->role != SPI_ROLE_MASTER ||
      slave->role != SPI_ROLE_SLAVE ||
      master->communication != SPI_COMM_FULL_DUPLEX ||
      slave->communication != SPI_COMM_RX_ONLY || master->data_size != 16U ||
      slave->data_size != 16U || master->mode != slave->mode ||
      master->bit_order != slave->bit_order)
    return SPI_STATUS_INVALID_ARGUMENT;

  SPI_TypeDef *m = master->spi;
  SPI_TypeDef *s = slave->spi;
  const uint32_t cs = 1UL << cs_pin;
  LL_GPIO_SetOutputPin(cs_port, cs);

  LL_SPI_Disable(m);
  LL_SPI_Disable(s);
  LL_SPI_SetTransferSize(s, 1U);
  LL_SPI_SetTransferSize(m, 1U);
  clear_flags(s);
  clear_flags(m);

  /* Slave ready first; SPI5 generates the clocks for both ADC outputs. */
  LL_SPI_Enable(s);
  LL_SPI_Enable(m);
  uint32_t start = time_ms();
  while ((m->SR & SPI_SR_TXP) == 0U) {
    if (time_expired(start, timeout_ms)) {
      abort_transfer(m);
      LL_SPI_Disable(s);
      return SPI_STATUS_TIMEOUT;
    }
  }
  write_frame(m, tx, 16U);
  LL_GPIO_ResetOutputPin(cs_port, cs);
  LL_SPI_StartMasterTransfer(m);

  bool got_a = false;
  bool got_b = false;
  spi_status_t result = SPI_STATUS_OK;
  for (;;) {
    uint32_t mr = m->SR;
    uint32_t sr = s->SR;
    if ((mr | sr) & SPI_SR_OVR) {
      result = SPI_STATUS_OVERRUN;
      break;
    }
    if ((mr | sr) & SPI_SR_MODF) {
      result = SPI_STATUS_MODE_FAULT;
      break;
    }
    if (!got_a && (mr & SPI_SR_RXP)) {
      *a = (uint16_t)read_frame(m, 16U);
      got_a = true;
    }
    if (!got_b && (sr & SPI_SR_RXP)) {
      *b = (uint16_t)read_frame(s, 16U);
      got_b = true;
    }
    /* Hardware-NSS slave EOT is not a reliable single-frame completion flag. */
    if (got_a && got_b && (m->SR & SPI_SR_EOT))
      break;
    if (time_expired(start, timeout_ms)) {
      result = SPI_STATUS_TIMEOUT;
      break;
    }
  }

  if (result != SPI_STATUS_OK)
    abort_transfer(m);
  LL_GPIO_SetOutputPin(cs_port, cs);
  LL_SPI_Disable(m);
  LL_SPI_Disable(s);
  clear_flags(m);
  clear_flags(s);
  LL_SPI_Enable(m);
  LL_SPI_Enable(s);
  return result;
}
