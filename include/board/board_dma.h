#pragma once

/* DMA2 is reserved for AD4683 acquisition (SPI5 TX/RX and SPI4 RX).
 * Stream setup and transfer state are owned by services/acquisition.c.
 */
void board_dma_adc_init(void);
void board_dma_adc_enable_irqs(void);
