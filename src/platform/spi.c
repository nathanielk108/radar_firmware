#include "platform/spi.h"
#include "platform/time.h"
#include "stm32h7xx_ll_gpio.h"
#include "stm32h7xx_ll_spi.h"
#include "stm32h7xx_ll_bus.h"

static bool spi_enable_clock(SPI_TypeDef *spi) {
    if      (spi == SPI1) LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SPI1);
    else if (spi == SPI2) LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_SPI2);
    else if (spi == SPI3) LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_SPI3);
    else if (spi == SPI4) LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SPI4);
    else if (spi == SPI5) LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SPI5);
    else if (spi == SPI6) LL_APB4_GRP1_EnableClock(LL_APB4_GRP1_PERIPH_SPI6);
    else return false;
    return true;
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

spi_status_t spi_init(const spi_config_t *config) {
    if (!config || !config->spi ||
        config->data_size < 4U || config->data_size > 32U ||
        config->mode > SPI_MODE_3 || config->baud_divider > SPI_DIV_256 ||
        config->communication > SPI_COMM_HALF_DUPLEX ||
        !spi_enable_clock(config->spi)) {
        return SPI_STATUS_INVALID_ARGUMENT;
    }

    SPI_TypeDef *spi = config->spi;
    LL_SPI_Disable(spi);
    spi->CR1 = 0U;
    spi->CR2 = 0U;
    spi->CFG1 = 0U;
    spi->CFG2 = 0U;

    LL_SPI_SetDataWidth(spi, (uint32_t)(config->data_size - 1U));
    LL_SPI_SetBaudRatePrescaler(spi,
        (uint32_t)config->baud_divider << SPI_CFG1_MBR_Pos);
    LL_SPI_SetMode(spi, config->role == SPI_ROLE_MASTER ?
                   LL_SPI_MODE_MASTER : LL_SPI_MODE_SLAVE);
    LL_SPI_SetTransferDirection(spi, (uint32_t)config->communication << SPI_CFG2_COMM_Pos);
    LL_SPI_SetClockPolarity(spi, (config->mode == SPI_MODE_2 || config->mode == SPI_MODE_3) ?
                            LL_SPI_POLARITY_HIGH : LL_SPI_POLARITY_LOW);
    LL_SPI_SetClockPhase(spi, (config->mode == SPI_MODE_1 || config->mode == SPI_MODE_3) ?
                         LL_SPI_PHASE_2EDGE : LL_SPI_PHASE_1EDGE);
    LL_SPI_SetTransferBitOrder(spi, config->bit_order == SPI_LSB_FIRST ?
                                LL_SPI_LSB_FIRST : LL_SPI_MSB_FIRST);
    LL_SPI_SetNSSMode(spi, config->software_nss ? LL_SPI_NSS_SOFT : LL_SPI_NSS_HARD_INPUT);
    if (config->software_nss) SET_BIT(spi->CR1, SPI_CR1_SSI);

    spi->IFCR = SPI_IFCR_EOTC | SPI_IFCR_TXTFC | SPI_IFCR_OVRC | SPI_IFCR_MODFC;
    LL_SPI_Enable(spi);
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
        (timeout_ms == 0U) ||
        (config->data_size < 4U) || (config->data_size > 32U) ||
        (config->communication != SPI_COMM_FULL_DUPLEX)) {
        return SPI_STATUS_INVALID_ARGUMENT;
    }

    SPI_TypeDef *spi = config->spi;
    if ((spi->CR1 & SPI_CR1_SPE) == 0U) {
        return SPI_STATUS_INVALID_ARGUMENT;
    }

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
    LL_SPI_SetTransferSize(spi, frame_count);

    spi_write_frame(spi, get_tx_frame(tx, 0, config->data_size), config->data_size);
    tx_count = 0;

    /*
     * Start master transaction.
     */
    if (config->role == SPI_ROLE_MASTER) {
        LL_SPI_StartMasterTransfer(spi);
    }

    while (rx_count < frame_count) {

        uint32_t status = spi->SR;

        /*
         * Check errors.
         */
        if ((status & SPI_SR_OVR) != 0U) {
            spi->IFCR = SPI_IFCR_OVRC;
            LL_SPI_Disable(spi);
            return SPI_STATUS_OVERRUN;
        }

        if ((status & SPI_SR_MODF) != 0U) {
            spi->IFCR = SPI_IFCR_MODFC;
            LL_SPI_Disable(spi);
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

        /* Only consume receive frames when RXP says data is available. */
        if (((status & SPI_SR_EOT) != 0U) && (rx_count == frame_count)) {
            break;
        }

        if (time_elapsed_ms(start) >= timeout_ms) {
            // LL_SPI_Disable(spi);
            return SPI_STATUS_TIMEOUT;
        }
    }

    /*
     * Wait until transfer is completely finished on the wire.
     */
    while ((spi->SR & SPI_SR_EOT) == 0U) {
        if (time_elapsed_ms(start) >= timeout_ms) {
            // LL_SPI_Disable(spi);
            return SPI_STATUS_TIMEOUT;
        }
    }

    spi->IFCR = SPI_IFCR_EOTC | SPI_IFCR_TXTFC;

    return SPI_STATUS_OK;
}


spi_status_t spi_transfer_pair(
    const spi_config_t *master,
    const spi_config_t *slave,
    uint16_t tx,
    uint16_t *master_rx,
    uint16_t *slave_rx,
    GPIO_TypeDef *cs_port,
    uint8_t cs_pin,
    uint32_t timeout_ms)
{
    if (!master || !slave || !master_rx || !slave_rx || !cs_port || cs_pin > 15U ||
        !timeout_ms || master->spi == slave->spi ||
        master->role != SPI_ROLE_MASTER ||
        slave->role != SPI_ROLE_SLAVE ||
        master->communication != SPI_COMM_FULL_DUPLEX ||
        slave->communication != SPI_COMM_RX_ONLY ||
        master->data_size != 16U ||
        slave->data_size != 16U ||
        master->mode != slave->mode ||
        master->bit_order != slave->bit_order ||
        master->spi != SPI5 || slave->spi != SPI4) {
        return SPI_STATUS_INVALID_ARGUMENT;
    }

    SPI_TypeDef *m = master->spi;
    SPI_TypeDef *s = slave->spi;

    // Start a fresh transaction on both peripherals.
    LL_SPI_Disable(m);
    LL_SPI_Disable(s);

    LL_SPI_SetTransferSize(m, 1U);
    LL_SPI_SetTransferSize(s, 1U);

    uint32_t clear =
        SPI_IFCR_EOTC | SPI_IFCR_TXTFC |
        SPI_IFCR_OVRC | SPI_IFCR_MODFC;

    m->IFCR = clear;
    s->IFCR = clear;

    // Arm the slave before starting the master.
    LL_SPI_Enable(s);
    LL_SPI_Enable(m);

    // Preload the master's transmit FIFO.
    spi_write_frame(m, tx, 16U);

    // ADC CS also drives the SPI4 NSS input.
    LL_GPIO_ResetOutputPin(cs_port, 1UL << cs_pin);

    LL_SPI_StartMasterTransfer(m);

    uint32_t start = time_ms();
    bool got_a = false;
    bool got_b = false;
    spi_status_t result = SPI_STATUS_OK;

    /* Hardware-NSS slave does not always assert EOT for a single frame. */
    while (!got_a || !got_b || !(m->SR & SPI_SR_EOT)) {

        uint32_t msr = m->SR;
        uint32_t ssr = s->SR;

        if ((msr | ssr) & SPI_SR_OVR) {
            result = SPI_STATUS_OVERRUN;
            break;
        }

        if ((msr | ssr) & SPI_SR_MODF) {
            result = SPI_STATUS_MODE_FAULT;
            break;
        }

        if (!got_a && (msr & SPI_SR_RXP)) {
            *master_rx = (uint16_t)spi_read_frame(m, 16U);
            got_a = true;
        }

        if (!got_b && (ssr & SPI_SR_RXP)) {
            *slave_rx = (uint16_t)spi_read_frame(s, 16U);
            got_b = true;
        }

        if (time_elapsed_ms(start) >= timeout_ms) {
            result = SPI_STATUS_TIMEOUT;
            break;
        }
    }

    // Do not release CS until the master finishes,
    // except when aborting a failed transaction.
    if (result == SPI_STATUS_OK) {
        LL_GPIO_SetOutputPin(cs_port, 1UL << cs_pin);
    } else {
        LL_SPI_SuspendMasterTransfer(m);

        uint32_t abort_start = time_ms();
        while ((m->SR & SPI_SR_SUSP) == 0U &&
               (m->SR & SPI_SR_EOT) == 0U &&
               time_elapsed_ms(abort_start) < timeout_ms) {
        }

        LL_GPIO_SetOutputPin(cs_port, 1UL << cs_pin);
    }

    LL_SPI_Disable(m);
    LL_SPI_Disable(s);

    m->IFCR = clear;
    s->IFCR = clear;

    return result;
}

