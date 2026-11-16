# RADAR firmware

Bare-metal STM32H743 firmware using CMSIS, STM32CubeH7 LL, and TinyUSB CDC.

## Startup

1. Inhibit charging, hold the display in reset with its backlight off, and
   disable the RF rails while the MCU clock, SysTick, MPU and caches start.
2. Configure the BQ25883, ISM330IS and BMI323.
3. Enable the RF board's 5 V rail, then its 15 V rail. After the rails settle,
   set the shared VGA gain DAC to a nominal 2.5 V and put the ADF4158 PLL
   in software power-down.
4. Configure the AD4683 for two-wire output and its external 3.3 V reference.
   Initialize the ADC timer and DMA.
5. Start USB CDC when the charger reports qualified VBUS. Open the native USB
   port to read the queued boot status messages. The PCB's PA9 VBUS divider
   produces about 1.6 V at 5 V input, so firmware uses the BQ25883 power-good
   signal on PG1 for USB attach and detach.

The foreground loop services SWO, USB and charging. The display and SDMMC
application paths are still pending.

## USB-C charging

ADC1 reads CC1 on PF12 and CC2 on PF11 against the board's 3.3 V reference.
After the BQ25883 qualifies VBUS, the firmware checks for one active CC pin
and a stable 5 V current advertisement. It programs the BQ25883 input limit
to 500, 1500 or 3000 mA as appropriate, with the hardware ILIM cap in place.
The Dantona L74A34-2-1-2W is a 7.4 V, 3400 mAh, 2S Li-ion pack. The charger
uses an 8.2 V ceiling, a 1.0 A nominal fast-charge target and 50 mA precharge
and termination settings. The 1.0 A target is the lower charger rating in
Dantona's 7.4 V pack-family catalog. USB input headroom and the system load
determine the actual battery current. [Dantona pack and charger catalog](https://www.farnell.com/datasheets/3759304.pdf)

The J4 pack thermistor feeds the BQ25883's JEITA protection. Cool charging
is limited to 20% of the current setting, and warm, hot and cold charging
are suspended. Charge termination and the 12-hour safety timer are enabled.
The charger also reduces current at its 80 °C die-temperature setting.
[BQ25883 datasheet](https://www.ti.com/lit/ds/symlink/bq25883.pdf)

Default-current sources enable charging after USB configuration and pause
during USB suspend. Explicit 1.5 A and 3 A sources can charge without a data
connection. A missing or ambiguous CC reading inhibits charging, and a lower
advertisement reduces the limit promptly. The BQ25883 system power path
remains available at its 500 mA minimum setting when charging is inhibited.
The firmware reads the 5 V advertisement without negotiating USB-PD.

## IF gain

PA4 (DAC1 channel 1) drives both AD8337 gain inputs. Send `g 2500` followed
by Enter over USB CDC to set the nominal DAC output in millivolts, or send
`g` followed by Enter to read the current setting. The buffered DAC accepts
200 to 3100 mV with the board's nominal 3.3 V reference. Adjust gain between
captures. Each capture records the setting as `gain_mv` in its CSV metadata.

The AD8337 gain is set relative to its VCOM pin. With VCOM at 2.5 V, a
1900 to 3100 mV DAC sweep covers the most linear part of the gain curve.
[AD8337 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/AD8337.pdf)

## Single chirp range check

SPI5 drives the ADC clock and receives channel A. SPI4 receives channel B as a
hardware-NSS slave. PF6 is GPIO chip select during ADC configuration and
TIM16_CH1 chip select during capture. DMA2 stream 0 writes one 16-bit
command to SPI5 at the TIM16_CH1 compare, which also drives CS low. At
400 kSPS, CS is high for 1.6 µs and low for 0.9 µs. SPI5 runs at 30 MHz.

Send lowercase `c` over the digital board's native USB CDC port to capture one
chirp. The firmware sends a metadata line followed by 61 rows of signed ADC
codes. Save the terminal output as `capture.csv` and run
`python scripts/chirp_range.py capture.csv` to estimate the strongest beat
frequency and corresponding range on each channel. Use a stationary reflector
at a known distance to check the estimates.

The ADF4158 uses TXDATA as its external step clock (R5.DB29), selects single
ramp mode, and holds the terminal frequency through the ADC drain frame.
Firmware allows 20 ms for initial PLL settling. TIM2_CH1 on PA0 then generates
120 rising edges at 800 kHz, with 1.25 MHz per step for a nominal
5725–5875 MHz sweep. TIM2_CH2 starts TIM16 through one DMA write. Both timers
stop in hardware.

The first retained conversion samples the settled start frequency nominally
625 ns before the first step. The last samples 625 ns after the last step.
The 61 conversions span 60 sample intervals, or 150 µs at 400 kSPS. The ADC
delivers each conversion on the following SPI frame, so TIM16 generates 62
frames. DMA stores the stale first word immediately before each aligned view,
and the final frame reads out the last retained sample.

The profile has a nominal slope of 1 THz/s and a theoretical resolution of
about 1 m from a 150 MHz sampled span. The 20 m proposal target corresponds to
a beat near 133 kHz, below the 200 kHz Nyquist limit. Scope TXDATA, PF6 and
SCLK together to measure the DMA start delay and ADC framing. Measure PLL
settling and RF tracking to establish the effective slope and sampled
bandwidth before interpreting a target measurement.

## Application buffers

`chirp_capture_one()` returns two 32-byte-aligned, read-only `int16_t` arrays
directly from non-cacheable D2 SRAM. The arrays hold one channel each, with
matching sample indices, and remain valid until the next burst. Application
code can consume the raw Q15 codes or convert straight into CMSIS-DSP working
buffers. USB formatting runs after the timers and DMA stop.

The continuous path still uses paired ping-pong DMA with one interrupt per
completed channel block. Release each view with
`acquisition_timed_release_block()` before DMA reuses it. The first returned
word of a newly started continuous run still belongs to the preceding ADC
transaction.

## Build

Install Arm GNU Toolchain, CMake and Ninja, then run:

```sh
git submodule update --init -- vendor/STM32CubeH7 vendor/tinyusb
git -C vendor/STM32CubeH7 submodule update --init -- Drivers/CMSIS/Device/ST/STM32H7xx Drivers/STM32H7xx_HAL_Driver
cmake --preset Debug
cmake --build --preset Debug
```

Top-level Git status and diff report changes to vendor commit pins. To see local
edits in a vendor checkout, run `git -C vendor/tinyusb status` for that checkout.

The ELF, BIN, HEX and map files are under `build/Debug`. The preset uses USB for
boot logs. Use `-DPRINTF_OUT=BOTH` when configuring to send them to SWO as well.
