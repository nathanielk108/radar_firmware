#include <stm32h7xx.h>

#include "platform/clock.h"
#include "platform/core.h"
#include "platform/debug_swo.h"
#include "platform/fault.h"
#include "platform/time.h"
#include "platform/gpio.h"
#include "platform/mpu.h"

#include "board/board_pins.h"
#include "board/board_power.h"

int main(void) {

  core_enable_faults();

  if (clock_init() != CLOCK_OK) {
    while (1) {}
  }

  mpu_init();

  core_enable_icache();
  core_enable_dcache();

  core_enable_cycle_counter();

  debug_swo_init();
  debug_swo_write("BOOT\r\n");

  if (!time_init()) {
    debug_swo_write("Time initialization failed\r\n");
    while (1) {}
  }

  board_power_init();

  uint32_t start;

  // start = time_ms();
  // board_rf_5v_enable(true);
  // while (!time_expired(start, 500)) {}
  // debug_swo_write("5V Enabled\r\n");
  //
  // start = time_ms();
  // board_rf_15v_enable(true);
  // while (!time_expired(start, 500)) {}
  // debug_swo_write("15V Enabled\r\n");

  __attribute__((section(".dma_buffer"), aligned(32), used))
  static volatile uint16_t test_dma_buffer[2];

  static volatile uint16_t test_stack_buffer[2];

  test_dma_buffer[0] = 0x1234;
  test_dma_buffer[1] = 0xA5A5;

  test_stack_buffer[0] = 0x1234;
  test_stack_buffer[1] = 0xA5A5;

  while (1) {
    if (time_expired(start, 1000)) {
      start = time_ms();
    }
  }
}
