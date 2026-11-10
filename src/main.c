#include <stm32h7xx.h>

#include "platform/clock.h"
#include "platform/core.h"
#include "platform/debug_swo.h"
#include "platform/time.h"
#include "platform/mpu.h"
#include "platform/usb.h"
#include "platform/gpio.h"
#include "platform/spi.h"
#include "platform/i2c.h"

#include "board/board_pins.h"
#include "board/board_power.h"
#include "board/board_spi.h"
#include "board/board_i2c.h"

#include "tusb.h"

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

  if (!usb_init()) {
    debug_swo_write("USB initialization failed\r\n");
    while (1) {}
  }

  // start = time_ms();
  // board_rf_5v_enable(true);
  // while (!time_expired(start, 500)) {}
  // debug_swo_write("5V Enabled\r\n");
  //
  // start = time_ms();
  // board_rf_15v_enable(true);
  // while (!time_expired(start, 500)) {}
  // debug_swo_write("15V Enabled\r\n");

  board_spi_init();
  if (spi_init(&imu1_spi_config) != SPI_STATUS_OK || spi_init(&imu2_spi_config) != SPI_STATUS_OK) {
    debug_swo_write("IMU SPI initialization failed\r\n");
    while (1) {}
  }

  board_i2c_init();
  if (i2c_init(&batt_i2c_config) != I2C_STATUS_OK) {
    debug_swo_write("BATT I2C initialization failed\r\n");
    while (1) {}
  }

  uint8_t tx[] = { 0x80, 0x00, 0x00 };

  uint8_t rx[sizeof(tx)];

  char buffer[1024];
  uint32_t start = time_ms();
  while (1) {
    tud_task();
    if (usb_console_connected()) {
      uint32_t read = usb_console_read(buffer, sizeof(buffer) - 1);
      buffer[read] = '\0';
      usb_console_write(buffer);
    }
    if (time_expired(start, 1000)) {
      gpio_write(IMU2_CS_PORT, IMU2_CS_PIN, false);
      spi_status_t result = spi_transfer(&imu2_spi_config, tx, rx, 3, 100);
      gpio_write(IMU2_CS_PORT, IMU2_CS_PIN, true);
      debug_swo_hex32((uint32_t)result);
      debug_swo_write("\r\n");
      debug_swo_hex32(rx[0]);
      debug_swo_write("\r\n");
      debug_swo_hex32(rx[1]);
      debug_swo_write("\r\n");
      debug_swo_hex32(rx[2]);
      debug_swo_write("\r\n\r\n");
      start = time_ms();
    }
  }
}
