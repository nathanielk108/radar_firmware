set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Prevent executable compilation to feed into linker script
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

find_program(CMAKE_C_COMPILER
  NAMES arm-none-eabi-gcc
  REQUIRED
)

find_program(CMAKE_ASM_COMPILER
  NAMES arm-none-eabi-gcc
  REQUIRED
)

find_program(CMAKE_OBJCOPY
  NAMES arm-none-eabi-objcopy
  REQUIRED
)

find_program(CMAKE_SIZE
  NAMES arm-none-eabi-size
  REQUIRED
)

set(CMAKE_EXECUTABLE_SUFFIX ".elf")
