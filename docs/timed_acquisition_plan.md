# TIM16-paced AD4683 acquisition (STM32H743)

This firmware now implements the **continuous ADC sampling architecture**, but
**not** FMCW chirp gating, complete radar frames, or TXDATA scheduling. It has
not been verified on a physical STM32/ADC board in this patch.

## Hardware allocation

| Peripheral | Function | Rate / data |
| --- | --- | --- |
| TIM16_CH1 / PF6 | AD4683 shared CS, low-active PWM | 400 kHz, low 1.0 us |
| TIM16 update | DMAMUX1 request for DMA2 Stream 0 | once per conversion |
| DMA2 Stream 0 | RAM NOP -> SPI5 TXDR | circular, 16-bit, NDTR=1 |
| SPI5 | 16-bit full-duplex master, 30-MHz SCLK | TSIZE=0, TX paced by timer |
| DMA2 Stream 1 | SPI5 RXDR -> A[2][N] | double-buffered |
| SPI4 | Hardware-NSS 16-bit slave, shared SCK/CS | TSIZE=0 |
| DMA2 Stream 2 | SPI4 RXDR -> B[2][N] | double-buffered |

The 240-MHz timer and 120-MHz SPI5 kernel clocks follow `platform/clock.c`.
The exact 400 kSPS configuration uses TIM16 PSC=0, ARR=599, CCR1=240.
The timers use hardware events, with **no per-sample interrupt**. DMA1/2
cannot access DTCM. Both RX arrays and the dummy TX word live in the MPU's
noncacheable `.dma_buffer` D2 SRAM.

The two RX IRQs publish a block only after both channels complete and their
DMA current targets match. A second block completing before the previous
one is released reports `ACQUISITION_OVERRUN` and stops TIM16. The block
pointers returned by `acquisition_timed_get_block()` belong to DMA; callers
must release each block quickly. Access to the polling ADC driver is forbidden
while timed acquisition is configured, even between starts.

## Current USB test

After the AD4683 external reference is configured via polling, `main.c`
configures TIM16/DMA and waits for a USB CDC connection. Every 2 seconds it:

1. Starts continuous capture.
2. Waits up to 10 ms for one complete 64-sample paired block.
3. Stops TIM16, SPI5/SPI4 and DMA (another partial conversion may be discarded).
4. Writes a header and `index,adc_a,adc_b` CSV lines to USB CDC **after**
   stopping so the DMA data are stable; no sample copies are made.

The application keeps running the battery and IMU diagnostics. Polling AD4683
diagnostics are skipped while timed acquisition owns the bus.

## Required scope/logic-analyzer checks before relying on data

1. PF6: CS must start high, fall once every 2.5 us, stay low 1.0 us, and
   return high on stop or error.
2. PF7: exactly **16 SCLK pulses per CS low** with no residual clocks outside
   the low interval. Measure delay from CS falling to first SCLK and from last
   SCLK to next CS falling. The ADC requires its datasheet-defined quiet time.
3. PF9: MOSI stays zero. PE4: SPI4 NSS receives the same CS waveform. SPI5
   and SPI4 receive one aligned 16-bit word per CS pulse.
4. Sweep sample rate downward (e.g. 100000 Hz), then test at 400000 Hz.
   Check `SPI_SR_OVR` and `MODF`, DMA error flags, lost/duplicate samples,
   and the two RX DMA target bits. A timer-triggered DMA TX write **does not
   check SPI TX FIFO readiness**. If the FIFO queues words or hardware stalls,
   the method needs revisiting (e.g. DMAMUX gating of SPI5 TX requests).
5. Verify the returned first result belongs to the **previous** ADC conversion,
   and compare a steady input against the original one-sample SPI path.
6. Verify the stop/restart sequence and ADC CS high after USB disconnect.

Do not assume successful USB CSV output proves conversion-timing compliance.

## Future FMCW integration

Gate the timer or capture around each ramp, preserve phase alignment to
ADF4158 TXDATA, and decide whether frames should be assembled with a higher
level consumer or copied by MDMA. Those features are deliberately excluded
from this patch.
