# RADAR firmware

Bare-metal STM32H743 firmware using CMSIS, STM32CubeH7 LL, and TinyUSB CDC.

## Startup

1. Inhibit charging, hold the display in reset with its backlight off, and
   disable the RF rails while the MCU clock, SysTick, MPU and caches start.
2. Configure the BQ25883, ISM330IS and BMI323.
3. Enable the RF board's 5 V rail, then its 15 V rail. These rails supply
   the AD4683 and its reference. Put the ADF4158 PLL in software power-down.
4. Configure the AD4683 for two-wire output and its external 3.3 V reference.
   Prepare TIM16 and DMA for 400 kSPS with 100 paired samples per block.
   Sampling starts only when the application calls `acquisition_timed_start()`.
5. Start USB CDC. Open the port to read the queued boot status messages.

The foreground loop services SWO and USB. Charging remains inhibited until the
pack and charge settings are verified. The display and SDMMC application paths
are still pending.

## ADC capture

SPI5 drives the ADC clock and receives channel A. SPI4 receives channel B as a
hardware-NSS slave. PF6 is GPIO chip select for polling and single conversions
and TIM16_CH1 chip select for timed capture. DMA2 stream 0 writes one 16-bit
command to SPI5 at the TIM16_CH1 compare, which also drives CS low. At
400 kSPS, CS is high for 1.6 µs and low for 0.9 µs. SPI5 runs at 30 MHz.
Streams 1 and 2 receive the channels in paired ping-pong buffers and interrupt
once per completed block. The linker places these buffers in the MPU's
non-cacheable D2 SRAM region.

Timed acquisition owns the shared SPI bus until `acquisition_timed_deinit()`
restores the polling configuration and GPIO chip select. Release each borrowed
block with `acquisition_timed_release_block()` before DMA reuses its buffer. The
AD4683 returns the previous conversion on each SPI frame. The first word of each
capture belongs to the preceding chip-select frame.

On the board, verify USB enumeration and reconnect, the rail sequence, the ADC
CS-to-clock timing and both channels' DMA alignment at 400 kSPS. The USB PHY
setup expects an external 3.3 V supply on VDD33USB. Verify PLL lock and RF
output when bringing up chirp generation.

## Build

Install Arm GNU Toolchain, CMake and Ninja, then run:

```sh
git submodule update --init --recursive -- vendor/STM32CubeH7 vendor/tinyusb
cmake --preset Debug
cmake --build --preset Debug
```

The ELF, BIN, HEX and map files are under `build/Debug`. The preset uses USB for
boot logs. Use `-DPRINTF_OUT=BOTH` when configuring to send them to SWO as well.
