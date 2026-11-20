cmake_minimum_required(VERSION 3.22)

project(RADAR
  LANGUAGES C ASM
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(TINYUSB ${PROJECT_ROOT}/vendor/tinyusb)

# STM32CubeH7 supplies one consistent CMSIS Device/Core + LL/HAL header set.
# No CubeIDE, CubeMX-generated initialization or HAL source files required.
set(STM32CUBE_H7
  ${PROJECT_ROOT}/vendor/STM32CubeH7
  CACHE PATH "STM32CubeH7 firmware tree"
)
set(CMSIS_CORE ${STM32CUBE_H7}/Drivers/CMSIS)
set(CMSIS_DEVICE ${CMSIS_CORE}/Device/ST/STM32H7xx)
set(STM32_HAL_DRIVER ${STM32CUBE_H7}/Drivers/STM32H7xx_HAL_Driver)
set(STM32_HAL_DRIVER_INC ${STM32_HAL_DRIVER}/Inc)
set(FATFS ${STM32CUBE_H7}/Middlewares/Third_Party/FatFs/src)
set(CMSIS_DSP ${PROJECT_ROOT}/vendor/CMSIS-DSP)
if(NOT EXISTS "${STM32_HAL_DRIVER_INC}/stm32h7xx_ll_gpio.h"
   OR NOT EXISTS "${CMSIS_DEVICE}/Include/stm32h7xx.h")
  message(FATAL_ERROR "STM32CubeH7 CMSIS/LL headers missing under ${STM32CUBE_H7}")
endif()
if(NOT EXISTS "${FATFS}/ff.c" OR NOT EXISTS "${CMSIS_DSP}/Source/CMakeLists.txt")
  message(FATAL_ERROR "Initialize the FatFs and CMSIS-DSP submodules as shown in README.md")
endif()

set(LINKER_SCRIPT
  ${PROJECT_ROOT}/linker/STM32H743ZITx_FLASH_RAM_D1.ld
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Firmware
set(FIRMWARE_SOURCES
  src/main.c
  src/app/radar_run.c
  src/platform/clock.c
  src/platform/core.c
  src/platform/debug_swo.c
  src/platform/fault.c
  src/platform/time.c
  src/platform/mpu.c
  src/platform/usb.c
  src/platform/spi.c
  src/platform/i2c.c
  src/platform/sdmmc.c
  src/board/board_power.c
  src/board/board_spi.c
  src/board/board_i2c.c
  src/board/board_freq.c
  src/board/board_gain.c
  src/board/board_usb.c
  src/board/board_usb_cc.c
  src/board/board_display.c
  src/board/board_dma.c
  src/board/board_timer.c
  src/usb/usb_descriptors.c
  src/drivers/bq25883.c
  src/drivers/adf4158.c
  src/drivers/ad4683.c
  src/drivers/ism330is.c
  src/drivers/bmi323.c
  src/drivers/st7789.c
  src/system/log.c
  src/system/firmware.c
  src/system/console.c
  src/services/acquisition.c
  src/services/chirp.c
  src/services/display.c
  src/services/motion.c
  src/services/recording.c
  src/services/storage.c
  src/services/usb_charge.c
)

set(VENDOR_SOURCES
  ${TINYUSB}/src/tusb.c
  ${TINYUSB}/src/common/tusb_fifo.c
  ${TINYUSB}/src/device/usbd.c
  ${TINYUSB}/src/class/cdc/cdc_device.c
  ${TINYUSB}/src/portable/synopsys/dwc2/dcd_dwc2.c
  ${TINYUSB}/src/portable/synopsys/dwc2/dwc2_common.c
  ${STM32_HAL_DRIVER}/Src/stm32h7xx_hal_sd.c
  ${STM32_HAL_DRIVER}/Src/stm32h7xx_ll_sdmmc.c
  ${FATFS}/ff.c
)

add_executable(radar_firmware
  ${FIRMWARE_SOURCES}
  ${VENDOR_SOURCES}
  ${CMSIS_DEVICE}/Source/Templates/system_stm32h7xx.c
  ${CMSIS_DEVICE}/Source/Templates/gcc/startup_stm32h743xx.s
)

set_target_properties(radar_firmware PROPERTIES
  OUTPUT_NAME radar_firmware.elf
  LINK_DEPENDS "${LINKER_SCRIPT}"
)

target_include_directories(radar_firmware PRIVATE
  ${PROJECT_ROOT}/include
  ${TINYUSB}/src
  ${CMSIS_CORE}/Core/Include
  ${CMSIS_DEVICE}/Include
  ${STM32_HAL_DRIVER_INC}
  ${FATFS}
)

set(MCU_FLAGS
  -mcpu=cortex-m7
  -mthumb
  -mfpu=fpv5-d16
  -mfloat-abi=hard
)

set(CMSISCORE ${CMSIS_CORE}/Core)
set(CMSISDSP_INSTALL OFF)
set(DISABLEFLOAT16 ON)
set(FASTBUILD ON)
add_subdirectory(${CMSIS_DSP}/Source cmsis-dsp EXCLUDE_FROM_ALL)
target_compile_options(CMSISDSP PRIVATE
  ${MCU_FLAGS} -O3 -ffunction-sections -fdata-sections
)
target_link_libraries(radar_firmware PRIVATE CMSISDSP m)

target_compile_options(radar_firmware PRIVATE
  ${MCU_FLAGS}
  -ffunction-sections
  -fdata-sections
  -Wall
  -Wextra
  -Wshadow
  -Wdouble-promotion
)

set(PRINTF_OUT "USB" CACHE STRING "Logging backend")
# The indices match the backend values in src/system/log.c.
set(LOG_BACKENDS NONE SWO USB BOTH)
set_property(CACHE PRINTF_OUT PROPERTY STRINGS ${LOG_BACKENDS})
list(FIND LOG_BACKENDS "${PRINTF_OUT}" LOG_BACKEND_ID)
if(LOG_BACKEND_ID EQUAL -1)
  message(FATAL_ERROR "PRINTF_OUT must be NONE, SWO, USB, or BOTH")
endif()
target_compile_definitions(radar_firmware PRIVATE
  STM32H743xx
  HSE_VALUE=25000000U
  USE_PWR_LDO_SUPPLY
  PRINTF_OUT=${LOG_BACKEND_ID}
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_definitions(radar_firmware PRIVATE FIRMWARE_DEBUG=1)
  target_compile_options(radar_firmware PRIVATE
    -Og
    -g3
  )
else()
  target_compile_options(radar_firmware PRIVATE
    -O2
  )
endif()

target_link_options(radar_firmware PRIVATE
  ${MCU_FLAGS}
  -T${LINKER_SCRIPT}

  -Wl,--gc-sections
  -Wl,-Map=${CMAKE_BINARY_DIR}/radar_firmware.map

  --specs=nano.specs
  --specs=nosys.specs
)

# Build Artifacts

add_custom_command(
  TARGET radar_firmware
  POST_BUILD
  BYPRODUCTS
    ${CMAKE_BINARY_DIR}/radar_firmware.bin
    ${CMAKE_BINARY_DIR}/radar_firmware.hex

  COMMAND ${CMAKE_OBJCOPY}
    -O binary
    $<TARGET_FILE:radar_firmware>
    ${CMAKE_BINARY_DIR}/radar_firmware.bin

  COMMAND ${CMAKE_OBJCOPY}
    -O ihex
    $<TARGET_FILE:radar_firmware>
    ${CMAKE_BINARY_DIR}/radar_firmware.hex

  COMMAND ${CMAKE_SIZE}
    $<TARGET_FILE:radar_firmware>
  VERBATIM
)

# Flashing and Debugging

set(PROGRAMMER "openocd")
set(INTERFACE_CONFIG "interface/stlink.cfg")
set(TARGET_CONFIG "target/stm32h7x.cfg")

add_custom_target(
  flash
  DEPENDS radar_firmware
)

add_custom_command(
  TARGET flash
  POST_BUILD
  COMMAND ${PROGRAMMER}
    -f ${INTERFACE_CONFIG}
    -f ${TARGET_CONFIG}
    -c "adapter speed 1000"
    -c "transport select swd"
    -c "program $<TARGET_FILE:radar_firmware> verify reset exit"
  COMMENT "Program Target with OpenOCD"
  VERBATIM
)
