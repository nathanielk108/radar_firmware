# RADAR firmware

Bare-metal STM32H743 firmware using CMSIS, CMSIS-DSP, STM32CubeH7, TinyUSB CDC,
and FatFs.

## Startup

1. Inhibit charging, hold the display in reset with its backlight off, and
   disable the RF rails while the MCU clock, SysTick, MPU and caches start.
2. Configure the BQ25883, ISM330IS and BMI323. Mount a preformatted microSD card
   if present at boot.
3. Try the RF board's 5 V rail, then its 15 V rail. After the rails settle,
   put the ADF4158 in software power-down. Configure the AD4683 for two-wire
   output with its external 3.3 V reference, then initialize its timer and DMA.
   Set the VGA DAC to 2.5 V on success. If ADC initialization fails, turn off
   both RF rails and continue in digital-only mode.
4. Start USB CDC when the charger reports qualified VBUS. Open the native USB
   port to read the queued boot status messages. The PCB's PA9 VBUS divider
   produces about 1.6 V at 5 V input, so firmware uses the BQ25883 power-good
   signal on PG1 for USB attach and detach.

`firmware_init()` and `firmware_poll()` handle board startup and foreground
services. Application code can use the drivers through the public `services`
headers. The LCD boots with reset asserted and its backlight off. Explicitly
enable it only when a panel is fitted. The two IMU samples use sensor-native
axes pending physical verification of their orientation on the assembled PCB.

## Console and SD recording

Open the digital board's native USB CDC port. Send `help` followed by Enter.
The `c` single-chirp shortcut works without Enter.

| Command | Behavior |
| --- | --- |
| `status`, `power`, `imu [1\|2]` | Read subsystem, USB-C/charger, and motion diagnostics. |
| `power adc` | Read the latest BQ25883 ADC snapshot, including VBUS, VBAT, VSYS, IBUS, ICHG, thermistor ratio, die temperature, and sample age. |
| `rails [5v\|on\|off]` | Read regulator enable states, enable just 5 V or both regulators for bench testing, or shut down both and disable the LCD and RF services. |
| `rf on`, `rf off`, `g [mV]` | Probe or disable the RF board and configure IF gain. |
| `c` | Send one chirp as USB CSV. Also queue the raw capture if SD is mounted. |
| `save [chirps [interval_us]]` | Save one complete capture to SD. Train limits are 2–16 chirps at intervals of 500–10000 µs. The default interval is 1000 µs. |
| `sd mount`, `sd test`, `sd eject` | Mount an existing FAT volume, write and read back a new scratch file, or flush and unmount. |
| `test dsp`, `test adc`, `test stream` | Check the CMSIS-DSP real FFT, a finite ADC-only DMA capture, or one second of continuous ADC DMA with USB and charger tasks active. |
| `test lcd`, `bl [0–100]`, `lcd off` | Draw solid RGB565 bars, read or set the backlight PWM duty in percent, or disable the panel. |
| `echo <text>` | Check the CDC receive and transmit paths. |

`test fault` deliberately triggers a UsageFault in Debug builds. Run it with a
debugger attached. `sd test` creates an `SDTESTnn.DAT` scratch file without
overwriting existing files. Use a preformatted FAT16/FAT32 card. If the card
was inserted after boot, run `sd mount`.

Diagnostics append to `DIAG.LOG`. Successful captures are saved as separate
`Cnnnnnnn.BIN` files with both channels, every retained sample, gain, programmed
sweep, and timing metadata. The foreground copies completed DMA views into a
four-capture queue. Run `recording_task()` after time-critical work. It writes
only while capture is idle. SDMMC1 uses internal DMA with a private cache-managed
AXI SRAM buffer. Card and FatFs waits still occupy the foreground during each
file write. `status` reports pending captures, losses, write errors, and the
last completed filename. Use `sd eject` before removing the card.

Convert the complete file on a host, or extract one chirp for the existing
range and spectrum tools.

```sh
python scripts/read_radar_capture.py C0000000.BIN --output train.csv
python scripts/read_radar_capture.py C0000000.BIN --chirp 0 --output chirp.csv
python scripts/plot_chirp_spectrum.py chirp.csv
```

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

Charging starts only after a fresh loaded BAT-pin measurement confirms a
plausible pack voltage with the J4 thermistor connected. The charger uses its
internal discharge load briefly while CE is inhibited to distinguish the pack
from the BAT-pin capacitor. During charging, firmware watches VBAT, the
thermistor, and charger status. A completed charge is rechecked before recharge
at 8.0 V. A missing or unconfirmed pack keeps charging inhibited while USB
continues to power the system.

Default-current sources enable charging after USB configuration and pause
during USB suspend. Explicit 1.5 A and 3 A sources can charge without a data
connection. A missing or ambiguous CC reading inhibits charging, and a lower
advertisement reduces the limit promptly. The BQ25883 system power path
remains available at its 500 mA minimum setting when charging is inhibited.
The firmware reads the 5 V advertisement without negotiating USB-PD.

## LCD bench power

`test lcd` requests the shared 5 V backlight supply and works with the RF board
disconnected. `bl 40` sets TIM4 CH1 on PD12 to 40% duty at 20 kHz. `bl` reads
the setting, and 0% turns the backlight off. The setting lasts until reset.
`lcd off` releases the LCD's 5 V request. An RF or manual request can keep the
regulator enabled. `rails 5v` and `rails on` assert regulator enables without
probing the RF board. `rails off` disables the panel and both regulator
enables. The reported enable states reflect GPIO commands.

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
codes. Save the terminal output as `capture.csv`. Run
`python scripts/chirp_range.py capture.csv --channel b --peaks 5` to compare
the main beat peaks and their levels on channel B. With no options, the tool
reports the strongest peak on each channel. For a range check, keep the radar
fixed and repeat captures with the reflector in place and removed. Check that a
peak near the expected frequency appears with the reflector and moves with it.

Run `python scripts/plot_chirp_spectrum.py` to plot channel B from the three
outdoor captures together. The script uses NumPy and Matplotlib. Pass CSV paths
to compare other captures, and use `--output spectrum.png` to save the plot.

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

## Offline MATLAB processing

Run the current recordings through `scripts/analyze_fmcw.m` with base MATLAB
R2020a or newer. From the repository root, run the following in MATLAB.

```matlab
run('scripts/analyze_fmcw.m')
```

The script processes channel B and writes plots, per-shot diagnostics, range
peaks, known-target comparisons and a MAT workspace under
`build/fmcw_analysis`. It runs both `allShots` and `activeOnly`, with the second
selection excluding shots below 200 ADC codes AC RMS. Every original shot
remains available for diagnostics. Change the cutoff or other settings through
`fmcwOptions` before running.

```matlab
fmcwOptions = struct('quietRmsCodes', 200, 'figureVisible', 'off');
run('scripts/analyze_fmcw.m')
```

Processing includes single-precision DC removal, optional linear detrending,
rectangular/Hann/Blackman spectra, incoherent power averaging, 64-point
CA-CFAR for the current 61-sample records, interpolated peaks, harmonic flags,
and matched-gain empty-scene power comparisons. The analysis uses the corrected
9, 15 and 24 ft log distances. Legacy outdoor CSV distances are unspecified.

Ranges use the programmed slope. The report includes known-range gate metrics
and the strongest overall peak. The configured CFAR false-alarm probability and
candidate-screen thresholds require empirical validation. The current captures
support range and acquisition-repeatability checks. Doppler requires timed,
phase-coherent chirps, AoA requires populated calibrated receivers, and mapping
requires synchronized pose data. `analysis_notes.txt` records the assumptions,
metric definitions and processing references.

## Application buffers

Use `chirp_capture_start()` or `chirp_capture_train_start()` followed by
`chirp_capture_poll()`. The PLL settle wait leaves foreground services running.
The timers and DMA pace the train. Each train interval has 120 sweep edges,
64 ADC frames, and a reset edge after the ADC window. Verify the actual RF
tracking, chirp-to-chirp phase repeatability, and reset settling on the assembled
RF board before using the train for coherent processing.

A completed capture exposes two 32-byte-aligned, read-only `int16_t` arrays
directly from non-cacheable D2 SRAM. Each chirp has `samples.count` valid
samples per channel and starts at `chirp_index * sample_stride`. The views
remain valid until the next burst is armed. Use them directly as raw Q15 data,
or convert into cacheable CMSIS-DSP scratch buffers when a kernel modifies its
input. Pass the capture to `recording_capture()` before reusing its views.

Continuous acquisition uses paired ping-pong DMA and discards the initial
priming block. Release each view with `acquisition_timed_release_block()`
before the next block completes. A 128-sample block at 400 kSPS gives a 320 µs
budget. Treat overruns as discontinuities. The SD recorder is for completed
bursts and trains. Indefinite raw logging during continuous capture needs a
separate throughput and latency qualification.

## Build

Install Arm GNU Toolchain, CMake and Ninja, then run:

```sh
git submodule update --init -- vendor/STM32CubeH7 vendor/tinyusb vendor/CMSIS-DSP
git -C vendor/STM32CubeH7 submodule update --init -- Drivers/CMSIS/Device/ST/STM32H7xx Drivers/STM32H7xx_HAL_Driver Middlewares/Third_Party/FatFs
cmake --preset Debug
cmake --build --preset Debug
```

Top-level Git status and diff report changes to vendor commit pins. To see local
edits in a vendor checkout, run `git -C vendor/tinyusb status` for that checkout.

The ELF, BIN, HEX and map files are under `build/Debug`. The preset uses USB for
boot logs and buffers diagnostics for SD. Use `-DPRINTF_OUT=BOTH` when
configuring to send logs to SWO as well.
