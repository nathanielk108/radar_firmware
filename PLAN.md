Given where you are now, I would bring the board up in the following order and treat each stage as a **gate**. Do not proceed until the current stage has a simple repeatable test.

1. **Finish the debug foundation: SWO/ITM first.** Configure PB3/trace support and get a single character/message out through SWO while GDB remains connected. Then add a minimal logging API such as debug character/string output; avoid full `printf` initially. Deliberately trigger a UsageFault or bad access and verify your fault handler lets you inspect `CFSR`, `HFSR`, stacked PC/LR, etc. Your pass condition is that you can reset, halt, run, break, inspect registers, and obtain runtime diagnostics without USB or any application peripheral.

2. **Add a real timebase.** Keep DWT `CYCCNT` for profiling, but add a system time source separately. A 1 kHz SysTick is sufficient initially: maintain a monotonically increasing millisecond counter and provide elapsed-time/time-out functionality. Verify it with a GPIO toggle measured on the scope; for example, toggle every 500 ms and verify the period. Do not use arbitrary delay loops anywhere after this point.

3. **Build the GPIO layer and central pin definition.** Put physical assignments in `board_pins.h` and generic STM32 GPIO manipulation in `platform/gpio.c`. Support input/output, alternate function, pull configuration, speed, and atomic set/reset. Then verify several representative pins: ordinary output, ordinary input, and eventually one alternate-function pin. This becomes the foundation for everything else rather than having each driver manipulate `MODER`, `AFR`, etc. independently.

4. **Bring up board-level power control before functional peripherals.** On reset, make the safe state explicit: RF 5 V off, RF 15 V off, and any other controlled loads off. Read the eFuse PG/fault/status signals and charger/status signals you routed. Under GDB/SWO, command each enable individually and measure the corresponding rail with a DMM/scope. Verify correct voltage, startup behavior, and that disabling actually removes the rail. This gives you a `board_power` layer before RF drivers start assuming their rails exist.

5. **Decide the DMA/cache memory architecture now.** The H743 has 512 KB AXI SRAM plus D2 SRAM1/2/3; ST specifically identifies the D2 SRAM blocks as useful for peripheral DMA buffers. Normal SRAM is cacheable by default once D-cache is enabled, and STM32H7 has no hardware cache coherency between CPU and DMA. [STMicroelectronics](https://www.st.com/resource/en/reference_manual/dm00314099.pdf?utm_source=chatgpt.com) For this project, I would initially reserve a linker section in D2 SRAM for DMA buffers and configure that region as **non-cacheable with the MPU**. That is simpler and harder to misuse than sprinkling cache clean/invalidate calls throughout early firmware. ST explicitly presents non-cacheable DMA buffers as one valid coherency solution. [STMicroelectronics](https://www.st.com/resource/en/application_note/an4839-level-1-cache-on-stm32f7-series-and-stm32h7-series-stmicroelectronics.pdf?utm_source=chatgpt.com) Once that works, enable D-cache globally and verify normal firmware still behaves identically. Later, if ADC processing shows a measurable performance reason, you can move selected buffers to cacheable memory and manage cache lines explicitly.

6. **Bring up USB CDC next.** At this point you have SWO available if USB fails. Configure the USB kernel clock, D+/D− GPIO alternate functions, interrupt, and USB peripheral board initialization yourself, but use TinyUSB for the USB protocol stack. TinyUSB already supports STM32H743 and separates board-specific configuration from USB class/application logic. [TinyUSB](https://docs.tinyusb.org/en/latest/getting_started.html?utm_source=chatgpt.com) Start with CDC only. Verify enumeration on the PC, bidirectional echo, repeated reconnects, resets while connected, and sustained transfers. Once this works, make CDC your normal interactive console while keeping SWO for low-level debugging.

7. **Build the generic peripheral transports before device drivers.** Implement SPI and I²C at the STM32/platform level, initially using polling. Your generic SPI layer should understand peripheral instance, clock source, prescaler, CPOL/CPHA, data width, and chip-select handling; I²C should provide reads/writes plus meaningful timeout/error results. Verify buses electrically before connecting driver behavior: scope SCLK, MOSI/MISO or SDA/SCL, confirm frequency, idle polarity, and transmitted patterns. Only after polling transfers work should you add DMA versions.

8. **Bring up simple digital ICs one at a time.** Start with devices that can return an unmistakable ID register: whichever IMUs you populated are ideal. Read WHO_AM_I/device ID repeatedly, then raw acceleration/gyro data. Verify stationary acceleration magnitude near 1 g, gyro near zero, then rotate each physical axis and confirm your coordinate mapping. Do not start fusion yet. Your device driver should know the IC protocol, while your SPI/I²C platform code knows STM32 hardware.

9. **Bring up the ADF4158 serial interface independently of radar operation.** First verify DATA/CLK/LE waveforms with a logic analyzer against the datasheet timing. Write known register configurations and confirm exact bit ordering. Then enable the RF supply and verify PLL/reference behavior using MUXOUT or RF measurement equipment. At this stage the driver should expose register/configuration functions; FMCW sweep generation belongs in a higher radar-control layer later.

10. **Bring up the AD4683 first without DMA.** Configure the ADC and capture a small number of conversions using polling so that you prove SPI framing, timing, both simultaneous outputs, and channel correspondence independently of DMA. Feed DC or a known low-frequency signal into each channel and compare captured codes to what you expect. Only after that works should you implement your unusual dual-SPI capture arrangement: SPI1 as clock/master for SDOA and SPI2 as synchronized slave receiver for SDOB.

11. **Then make ADC acquisition DMA-driven.** Put both receive buffers in your reserved DMA SRAM. Start with one finite capture, verify exact sample count and channel alignment, then move to ping-pong/double-buffered acquisition. Generate a known sinusoid and export raw samples through USB or SD; compare the STM32 samples against a PC/MATLAB FFT. Run continuous acquisition long enough to prove zero overruns. This is also where your event-loop architecture starts taking shape: DMA ISR should only acknowledge the event and mark a buffer ready; processing occurs outside the ISR.

12. **Bring up the display separately.** Write a low-level ST7789/8080 bus driver, then draw simple deterministic patterns: black, white, RGB primaries, checkerboard, coordinate grid. Verify orientation and pixel addressing before creating text/widgets/maps. Keep the framebuffer/rendering API separate from the physical display bus so mapping code later does not know anything about GPIO timing.

13. **Bring up microSD/SDMMC as a block device first.** Prove card initialization, block write, block read, and bit-for-bit verification before adding a filesystem. Then add FatFs rather than writing FAT yourself; filesystem implementation has essentially no capstone value. Perform repeated large sequential writes because your eventual use is likely raw radar/logging data rather than tiny configuration files. Verify that SD writes do not starve radar DMA or introduce unacceptable latency.

14. **Add interrupt/EXTI support and formalize the event loop.** At this point you will have asynchronous events from DMA, USB, timers, potentially IMU data-ready pins, and power faults. Keep ISRs minimal: capture status/timestamp, clear the hardware condition, and set an event/queue flag. Your main loop should dispatch work. Stay bare-metal here unless this becomes genuinely difficult; nothing in this architecture yet requires an RTOS.

15. **Create a board self-test/bring-up mode before application code.** By this stage your USB console should be able to invoke independent tests such as GPIO, power rails, IMU ID/raw samples, PLL programming, ADC capture, display pattern, SD read/write, and DMA stress tests. This is extremely valuable when you assemble a second PCB: instead of running the radar application and asking why it fails, you can validate every subsystem independently.

The resulting boundary should look approximately like this:

```text
app/                 ← not started yet
radar/
fusion/
mapping/

drivers/             ← AD4683, IMUs, ADF4158, LCD, SD
services/            ← acquisition, power, logging, console

platform/            ← clock, core, MPU/cache, GPIO,
                        time, IRQ, SPI, I2C, DMA, USB, SDMMC

vendor/              ← CMSIS, CMSIS-DSP, TinyUSB, FatFs
```

I would call the board **application-ready** when you can power it on and demonstrate, independently: SWO/GDB diagnostics; accurate timebase; D-cache enabled with a proven DMA strategy; USB CDC console; power-rail sequencing; both IMUs returning valid samples; ADF4158 register programming; synchronized two-channel AD4683 DMA acquisition with no overruns; LCD test patterns; and SD block/file read-write.

Only then start the actual radar application. The first application milestone should be very narrow: **command one chirp configuration → capture one ADC frame → send the raw frame to the PC**. Range FFT, Doppler, CFAR, AoA, IMU fusion, mapping, and display integration can then be added one validated stage at a time. [STMicroelectronics](https://www.st.com/resource/en/reference_manual/rm0433-stm32h743-753-and-stm32h750-value-line-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf?utm_source=chatgpt.com)
