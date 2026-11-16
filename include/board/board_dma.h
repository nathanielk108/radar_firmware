#pragma once

/* DMA2 streams 0-2 are owned by AD4683 acquisition. The timer board helper
 * uses streams 3-4 for chirp control.
 */
void board_dma_adc_init(void);
void board_dma_adc_enable_irqs(void);
