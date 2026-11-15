# RADAR firmware

Bare-metal STM32H743 firmware. CMSIS + STM32CubeH7 LL for peripheral setup,
TinyUSB for USB CDC, and CMSIS-DSP for future processing. No RTOS.

## Source layout

- `src/platform/`, `include/platform/`: generic MCU peripheral operations,
  clock, timebase, USB, faults and MPU.
- `src/board/`, `include/board/`: physical pins, peripheral instances,
  power sequencing and fixed DMA/NVIC allocation.
- `src/drivers/`, `include/drivers/`: register-level device protocols for
  AD4683, ADF4158, BQ25883, ISM330IS and BMI323.
- `src/services/`, `include/services/`: coordinated operations that use more
  than one peripheral, starting with AD4683 SPI4/SPI5 DMA capture.
- `src/system/`, `include/system/`: logging and diagnostic infrastructure.
- `linker/`: linker script; `.dma_buffer` is noncacheable D2 SRAM.

## Current ADC DMA behavior

`services/acquisition.c` owns **all three DMA2 streams and their interrupts**.
The working `acquisition_capture_one()` API remains available and uses normal
DMA for a single paired 16-bit conversion result.

`acquisition_timed_init/start/get_block/release_block/stop/deinit()` implements
TIM16-paced SPI5 TX and double-buffered DMA for both ADC channels:

| DMA2 stream | Trigger | Direction and mode |
| --- | --- | --- |
| 0 | TIM16 update | NOP -> SPI5 TXDR, circular, NDTR=1 |
| 1 | SPI5 RX | RXDR -> ADC A ping/pong, double buffer |
| 2 | SPI4 RX | RXDR -> ADC B ping/pong, double buffer |

PF6 is software GPIO CS for polling/single conversion and becomes TIM16_CH1
(active-low PWM) for timed acquisition. `board_timer.c` enables its clock; the
acquisition service owns the sampling and DMA configuration. Timer IRQs and
TX DMA completion IRQs are **not** used at 400 kSPS. DMA RX interrupts are
per completed *block*, not per sample.

`main.c` samples a bounded 64-pair block at 400 kSPS and then prints it as
CSV over USB CDC. It waits for a USB connection. The data are read directly
from the noncacheable D2 SRAM DMA buffer **after sampling is stopped**.

**Bus ownership:** Polling ADC register/diagnostic operations are prohibited
whenever timed acquisition is configured. Configure the 3.3-V external
reference before `acquisition_timed_init()`; call `acquisition_timed_deinit()`
to restore polling SPI and GPIO CS operation.

**Hardware safety:** The AD4683 is on the RF board and requires both RF supply
rails. Charger CE inhibit remains asserted during boot until verified.

**Hardware validation:** This code has not been tested on the actual board.
See [docs/timed_acquisition_plan.md](docs/timed_acquisition_plan.md) for
specific CS, SCLK, FIFO and sample-alignment checks required at 400 kSPS.

## Build

Initialize the submodules (not packaged in the source-only ZIP):

```sh
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
cmake --build build
```
