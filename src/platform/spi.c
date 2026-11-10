#include "platform/spi.h"
#include "platform/time.h"

static bool spi_enable_clock(SPI_TypeDef *spi) {
  if (spi == SPI1) {
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    (void)RCC->APB2ENR;
    return true;
  }
  else if (spi == SPI2) {
    RCC->APB1LENR |= RCC_APB1LENR_SPI2EN;
    (void)RCC->APB1LENR;
    return true;
  }
  else if (spi == SPI3) {
    RCC->APB1LENR |= RCC_APB1LENR_SPI3EN;
    (void)RCC->APB1LENR;
    return true;
  }
  else if (spi == SPI4) {
    RCC->APB2ENR |= RCC_APB2ENR_SPI4EN;
    (void)RCC->APB2ENR;
    return true;
  }
  else if (spi == SPI5) {
    RCC->APB2ENR |= RCC_APB2ENR_SPI5EN;
    (void)RCC->APB2ENR;
    return true;
  }
  else if (spi == SPI6) {
    RCC->APB4ENR |= RCC_APB4ENR_SPI6EN;
    (void)RCC->APB4ENR;
    return true;
  }

  return false;
}

__attribute__((__unused__))
static spi_status_t spi_wait_flag(SPI_TypeDef *spi, uint32_t flag, uint32_t start, uint32_t timeout_ms) {
  while ((spi->SR & flag) == 0U) {
    if ((spi->SR & SPI_SR_OVR) != 0U) {
      spi->IFCR = SPI_IFCR_OVRC;
      return SPI_STATUS_OVERRUN;
    }

    if ((spi->SR & SPI_SR_MODF) != 0U) {
      spi->IFCR = SPI_IFCR_MODFC;
      return SPI_STATUS_MODE_FAULT;
    }

    if (time_elapsed_ms(start) >= timeout_ms) {
      return SPI_STATUS_TIMEOUT;
    }
  }

  return SPI_STATUS_OK;
}

static void spi_write_frame(SPI_TypeDef *spi, uint32_t value, uint8_t data_size) {
  if (data_size <= 8U) {
    *((volatile uint8_t *)&spi->TXDR) = (uint8_t)value;
  }
  else if (data_size <= 16U) {
    *((volatile uint16_t *)&spi->TXDR) = (uint16_t)value;
  }
  else {
    *((volatile uint32_t *)&spi->TXDR) = value;
  }
}

static uint32_t spi_read_frame(SPI_TypeDef *spi, uint8_t data_size) {
  if (data_size <= 8U) {
    return *((volatile uint8_t *)&spi->RXDR);
  }

  if (data_size <= 16U) {
    return *((volatile uint16_t *)&spi->RXDR);
  }

  return *((volatile uint32_t *)&spi->RXDR);
}

static uint32_t get_tx_frame(const void *buffer, uint16_t index, uint8_t data_size) {
  if (buffer == NULL) {
    return 0xFFFFFFFFU;
  }

  if (data_size <= 8U) {
    return ((const uint8_t *)buffer)[index];
  }

  if (data_size <= 16U) {
    return ((const uint16_t *)buffer)[index];
  }

  return ((const uint32_t *)buffer)[index];
}

static void store_rx_frame(void *buffer, uint16_t index, uint8_t data_size, uint32_t value) {
  if (buffer == NULL) {
    return;
  }

  if (data_size <= 8U) {
    ((uint8_t *)buffer)[index] = (uint8_t)value;
  }
  else if (data_size <= 16U) {
    ((uint16_t *)buffer)[index] = (uint16_t)value;
  }
  else {
    ((uint32_t *)buffer)[index] = value;
  }
}

spi_status_t spi_init(spi_config_t *config) {
  if ((config == NULL) || (config->spi == NULL) || (config->data_size < 4U) || (config->data_size > 32U)) {
    return SPI_STATUS_INVALID_ARGUMENT;
  }

  if (!spi_enable_clock(config->spi)) {
    return SPI_STATUS_INVALID_ARGUMENT;
  }

  SPI_TypeDef *spi = config->spi;

  /* Peripheral must be disabled while configuring it. */
  spi->CR1 &= ~SPI_CR1_SPE;

  /*
   * Start from a defined state.
   * We are not using CRC or DMA yet.
   */
  spi->CR1 = 0U;
  spi->CR2 = 0U;
  spi->CFG1 = 0U;
  spi->CFG2 = 0U;

  /*
   * CFG1:
   * DSIZE is encoded as number_of_bits - 1.
   * FTHLV = 0 gives a one-frame FIFO threshold, convenient
   * for simple polling.
   */
  spi->CFG1 = ((uint32_t)config->baud_divider << SPI_CFG1_MBR_Pos) |
              ((uint32_t)(config->data_size - 1U) << SPI_CFG1_DSIZE_Pos);

  /*
   * Software NSS: set SSI before enabling software NSS/master.
   */
  if (config->software_nss) {
    spi->CR1 |= SPI_CR1_SSI;
    spi->CFG2 |= SPI_CFG2_SSM;
  }

  if (config->role == SPI_ROLE_MASTER) {
    spi->CFG2 |= SPI_CFG2_MASTER;
  }

  spi->CFG2 |= ((uint32_t)config->communication << SPI_CFG2_COMM_Pos);

  if (config->bit_order == SPI_LSB_FIRST) {
    spi->CFG2 |= SPI_CFG2_LSBFRST;
  }

  switch (config->mode) {
  case SPI_MODE_0:
    break;

  case SPI_MODE_1:
    spi->CFG2 |= SPI_CFG2_CPHA;
    break;

  case SPI_MODE_2:
    spi->CFG2 |= SPI_CFG2_CPOL;
    break;

  case SPI_MODE_3:
    spi->CFG2 |= SPI_CFG2_CPOL | SPI_CFG2_CPHA;
    break;
  }

  /*
   * Clear stale status flags.
   */
  spi->IFCR = SPI_IFCR_EOTC | SPI_IFCR_TXTFC | SPI_IFCR_OVRC | SPI_IFCR_MODFC;

  spi->CR1 |= SPI_CR1_SPE;

  return SPI_STATUS_OK;
}

spi_status_t spi_transfer(
    const spi_config_t *config,
    const void *tx,
    void *rx,
    uint16_t frame_count,
    uint32_t timeout_ms)
{
    if ((config == NULL) ||
        (config->spi == NULL) ||
        (frame_count == 0U) ||
        (config->communication != SPI_COMM_FULL_DUPLEX)) {
        return SPI_STATUS_INVALID_ARGUMENT;
    }

    SPI_TypeDef *spi = config->spi;

    uint16_t tx_count = 0U;
    uint16_t rx_count = 0U;

    uint32_t start = time_ms();

    /*
     * Clear previous transfer flags.
     */
    spi->IFCR =
        SPI_IFCR_EOTC |
        SPI_IFCR_TXTFC |
        SPI_IFCR_OVRC |
        SPI_IFCR_MODFC;

    /*
     * Program number of frames for this transaction.
     */
    spi->CR2 =
        (spi->CR2 & ~SPI_CR2_TSIZE_Msk) |
        ((uint32_t)frame_count << SPI_CR2_TSIZE_Pos);

    /*
     * Start master transaction.
     */
    if (config->role == SPI_ROLE_MASTER) {
        spi->CR1 |= SPI_CR1_CSTART;
    }

    while (rx_count < frame_count) {

        uint32_t status = spi->SR;

        /*
         * Check errors.
         */
        if ((status & SPI_SR_OVR) != 0U) {
            spi->IFCR = SPI_IFCR_OVRC;
            return SPI_STATUS_OVERRUN;
        }

        if ((status & SPI_SR_MODF) != 0U) {
            spi->IFCR = SPI_IFCR_MODFC;
            return SPI_STATUS_MODE_FAULT;
        }

        /*
         * Feed TX FIFO whenever it can accept another frame.
         */
        if (((status & SPI_SR_TXP) != 0U) &&
            (tx_count < frame_count)) {

            uint32_t value = get_tx_frame(tx, tx_count, config->data_size);

            spi_write_frame(spi, value, config->data_size);

            ++tx_count;
        }

        /*
         * Read normal RX packets.
         */
        if (((status & SPI_SR_RXP) != 0U) &&
            (rx_count < frame_count)) {

            uint32_t value = spi_read_frame(spi, config->data_size);

            store_rx_frame(rx, rx_count, config->data_size, value);

            ++rx_count;
        }

        /*
         * At EOT, final received data may remain in RxFIFO
         * even though RXP is no longer asserted.
         */
        if ((status & SPI_SR_EOT) != 0U) {

            while (rx_count < frame_count) {

                uint32_t value = spi_read_frame(spi, config->data_size);

                store_rx_frame(rx, rx_count, config->data_size, value);

                ++rx_count;
            }

            break;
        }

        if (time_elapsed_ms(start) >= timeout_ms) {
            return SPI_STATUS_TIMEOUT;
        }
    }

    /*
     * Wait until transfer is completely finished on the wire.
     */
    while ((spi->SR & SPI_SR_EOT) == 0U) {
        if (time_elapsed_ms(start) >= timeout_ms) {
            return SPI_STATUS_TIMEOUT;
        }
    }

    spi->IFCR = SPI_IFCR_EOTC | SPI_IFCR_TXTFC;

    return SPI_STATUS_OK;
}
