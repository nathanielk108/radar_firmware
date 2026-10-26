### Filesystem Structure:
radar-firmware/
├── CMakeLists.txt
├── cmake/
│   └── arm-none-eabi.cmake
├── linker/
│   └── STM32H743ZITx_FLASH.ld
│
├── vendor/
│   ├── CMSIS_6/
│   ├── cmsis-device-h7/
│   └── CMSIS-DSP/
│
├── include/
│   ├── board/
│   │   ├── board.h
│   │   ├── board_pins.h
│   │   └── board_clocks.h
│   └── drivers/
│
├── src/
│   ├── main.c
│   │
│   ├── platform/
│   │   ├── clock.c
│   │   ├── gpio.c
│   │   ├── mpu.c
│   │   ├── cache.c
│   │   ├── time.c
│   │   └── interrupts.c
│   │
│   ├── drivers/
│   │   ├── adc_ad4683.c
│   │   ├── imu_ism330is.c
│   │   ├── imu_bmi323.c
│   │   ├── pll_adf4158.c
│   │   ├── display.c
│   │   └── sdcard.c
│   │
│   ├── radar/
│   ├── fusion/
│   └── app/
│
└── tests/
    └── host/
