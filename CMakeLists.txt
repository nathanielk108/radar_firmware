cmake_minimum_required(VERSION 3.22)

project(RADAR
  LANGUAGES C ASM
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

set(CMSIS_CORE
  ${PROJECT_ROOT}/vendor/CMSIS_6/CMSIS/Core
)

set(CMSIS_DEVICE
  ${PROJECT_ROOT}/vendor/cmsis-device-h7
)

set(CMSIS_DSP
  ${PROJECT_ROOT}/vendor/CMSIS-DSP
)

set(LINKER_SCRIPT
  ${PROJECT_ROOT}/linker/STM32H743ZITx_FLASH_RAM_D1.ld
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Common STM32H743 Build Configuration

add_library(mcu_options INTERFACE)

target_compile_options(mcu_options INTERFACE
  -mcpu=cortex-m7
  -mthumb
  -mfpu=fpv5-d16
  -mfloat-abi=hard

  # Places functions and variables into separate sections of object file
  -ffunction-sections
  -fdata-sections
)

target_link_options(mcu_options INTERFACE
  -mcpu=cortex-m7
  -mthumb
  -mfpu=fpv5-d16
  -mfloat-abi=hard
)

target_compile_definitions(mcu_options INTERFACE
  STM32H743xx
  HSE_VALUE=25000000U
  ARM_MATH_CM7
)

# CMSIS-DSP

set(CMSISCORE ${CMSIS_CORE})

set(DISABLEFLOAT16 ON CACHE BOOL "" FORCE)
set(CMSISDSP_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory(
  ${CMSIS_DSP}/Source
  ${CMAKE_BINARY_DIR}/cmsis-dsp
)

target_link_libraries(CMSISDSP PRIVATE
  mcu_options
)

target_compile_options(CMSISDSP PRIVATE
  -O3
  -ffast-math
)

# Firmware
add_executable(radar_firmware
  src/main.c

  ${CMSIS_DEVICE}/Source/Templates/system_stm32h7xx.c
  ${CMSIS_DEVICE}/Source/Templates/gcc/startup_stm32h743xx.s
)

set_target_properties(radar_firmware
  PROPERTIES OUTPUT_NAME radar_firmware.elf
)

target_include_directories(radar_firmware PRIVATE
  ${CMSIS_CORE}/Include
  ${CMSIS_DEVICE}/Include
  ${PROJECT_ROOT}/include
)

target_link_libraries(radar_firmware PRIVATE
  mcu_options
  CMSISDSP
)

target_compile_options(radar_firmware PRIVATE
  -Wall
  -Wextra
  -Wshadow
  -Wdouble-promotion
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
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
)
