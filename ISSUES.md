## **Problem Description: STM32H743 Timed Dual-Channel ADC Acquisition**

### **System architecture**

The system uses an STM32H743 to acquire simultaneous 16-bit samples from an AD4683 dual-channel ADC.

The existing polling-based and single-conversion DMA implementations successfully read both ADC channels. The current development objective is **continuous, hardware-timed acquisition at 400 kSPS without per-sample CPU intervention**.

The timed acquisition architecture consists of:

| Component           | Configuration                                                                       |
| ------------------- | ----------------------------------------------------------------------------------- |
| TIM16               | Generates ADC CS PWM and periodic DMA requests                                      |
| PF6 / TIM16_CH1     | ADC CS output                                                                       |
| SPI5                | Full-duplex master; generates SCLK and receives channel A                           |
| SPI4                | Slave; receives channel B using the shared SCLK                                     |
| DMA2 Stream 0       | Circular TX DMA, triggered by TIM16 update, repeatedly writes `0x0000` to SPI5 TXDR |
| DMA2 Stream 1       | Double-buffered SPI5 RX DMA                                                         |
| DMA2 Stream 2       | Double-buffered SPI4 RX DMA                                                         |
| RX buffers          | Two alternating buffers per channel, currently 64 samples per buffer                |
| Acquisition service | Coordinates DMA completion, buffer ownership, errors, and start/stop operations     |

The intended operation is for TIM16 to initiate one SPI transfer per ADC conversion. Both RX DMA streams independently fill alternating buffers. Once both channels complete a corresponding buffer, the acquisition service makes that sample block available to the CPU for processing without copying.

### **Observed problem**

**Timed acquisition consistently terminates with&#x20;**`ACQUISITION_OVERRUN`**, including when the sampling rate is reduced to 100 kSPS.**

At the point of failure, diagnostic counters reported:

```
SPI5 RX (Channel A) completed blocks: 2
SPI4 RX (Channel B) completed blocks: 1
Released blocks:                     0
```

This indicates that Channel A has completed a second DMA block while Channel B has completed only one, and the application has not released any completed block.

The acquisition service detects this as a potential buffer overwrite and aborts acquisition.

### **Confirmed findings**

- Single-conversion DMA acquisition works correctly.
- TIM16 PWM generation works when tested independently.
- The buffer overrun persists at 100 kSPS, where each 64-sample buffer should take 640 µs to fill.
- The observed completion counters are not synchronized at the point of failure.
- An initial observation of CS remaining high may have resulted from the acquisition service stopping TIM16 immediately after detecting the overrun.

### **Unresolved questions**

It has not yet been established whether the failure originates from:

1. **SPI/DMA synchronization:** SPI5 and SPI4 receive different numbers of frames, or their DMA completion events become misaligned.
2. **Buffer management:** The acquisition service incorrectly determines when paired blocks are ready, available, or safe to reuse.
3. **Foreground scheduling:** The CPU does not retrieve and release completed blocks before DMA needs to reuse them.
4. **Timer-driven SPI behavior:** Timer-triggered TX DMA writes produce unexpected SPI transfers or FIFO behavior.

The completion counts alone do not establish which mechanism is responsible.

### **Required outcome**

The system must reliably acquire synchronized, continuous sample blocks from both ADC channels, using hardware-timed SPI transactions and double-buffered DMA. The CPU should receive block-level completion notifications, process completed buffers directly, and return ownership before DMA reuses them.

**The immediate task is to identify why SPI5 RX advances ahead of SPI4 RX or why completed buffers are not consumed, without weakening the overrun protection or introducing per-sample interrupts.**

Chirp generation, FMCW synchronization, and radar signal processing are outside the scope of the current debugging effort.
