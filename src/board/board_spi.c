#include "platform/gpio.h"
#include "board/board_spi.h"
#include "board/board_pins.h"

const spi_config_t imu1_spi_config = {
  .spi = SPI1,
  .role = SPI_ROLE_MASTER,
  .communication = SPI_COMM_FULL_DUPLEX,
  .mode = SPI_MODE_0,
  .bit_order = SPI_MSB_FIRST,
  .baud_divider = SPI_DIV_16,
  .data_size = 8U,
  .software_nss = true,
};

const spi_config_t imu2_spi_config = {
  .spi = SPI2,
  .role = SPI_ROLE_MASTER,
  .communication = SPI_COMM_FULL_DUPLEX,
  .mode = SPI_MODE_0,
  .bit_order = SPI_MSB_FIRST,
  .baud_divider = SPI_DIV_16,
  .data_size = 8U,
  .software_nss = true,
};

void board_spi_init(void)
{
  gpio_config_t imu1_sck = {
      .port        = IMU1_SPI_SCK_PORT,
      .pin         = IMU1_SPI_SCK_PIN,
      .mode        = GPIO_MODE_ALTERNATE,
      .output_type = GPIO_PUSH_PULL,
      .pull        = GPIO_PULL_NONE,
      .speed       = GPIO_SPEED_HIGH,
      .alternate   = IMU1_SPI_SCK_AF
  };

  gpio_config_t imu1_miso = {
      .port        = IMU1_SPI_MISO_PORT,
      .pin         = IMU1_SPI_MISO_PIN,
      .mode        = GPIO_MODE_ALTERNATE,
      .output_type = GPIO_PUSH_PULL,
      .pull        = GPIO_PULL_NONE,
      .speed       = GPIO_SPEED_HIGH,
      .alternate   = IMU1_SPI_MISO_AF
  };

  gpio_config_t imu1_mosi = {
      .port        = IMU1_SPI_MOSI_PORT,
      .pin         = IMU1_SPI_MOSI_PIN,
      .mode        = GPIO_MODE_ALTERNATE,
      .output_type = GPIO_PUSH_PULL,
      .pull        = GPIO_PULL_NONE,
      .speed       = GPIO_SPEED_HIGH,
      .alternate   = IMU1_SPI_MOSI_AF
  };

  /*
   * CS should start inactive/high before it becomes an output.
   */
  gpio_config_t imu1_cs = {
      .port        = IMU1_CS_PORT,
      .pin         = IMU1_CS_PIN,
      .mode        = GPIO_MODE_OUTPUT,
      .output_type = GPIO_PUSH_PULL,
      .pull        = GPIO_PULL_NONE,
      .speed       = GPIO_SPEED_LOW,
      .alternate   = 0U,
      .init_state  = true
  };

  gpio_config_t imu2_sck = {
      .port        = IMU2_SPI_SCK_PORT,
      .pin         = IMU2_SPI_SCK_PIN,
      .mode        = GPIO_MODE_ALTERNATE,
      .output_type = GPIO_PUSH_PULL,
      .pull        = GPIO_PULL_NONE,
      .speed       = GPIO_SPEED_HIGH,
      .alternate   = IMU2_SPI_SCK_AF
  };

  gpio_config_t imu2_miso = {
      .port        = IMU2_SPI_MISO_PORT,
      .pin         = IMU2_SPI_MISO_PIN,
      .mode        = GPIO_MODE_ALTERNATE,
      .output_type = GPIO_PUSH_PULL,
      .pull        = GPIO_PULL_NONE,
      .speed       = GPIO_SPEED_HIGH,
      .alternate   = IMU2_SPI_MISO_AF
  };

  gpio_config_t imu2_mosi = {
      .port        = IMU2_SPI_MOSI_PORT,
      .pin         = IMU2_SPI_MOSI_PIN,
      .mode        = GPIO_MODE_ALTERNATE,
      .output_type = GPIO_PUSH_PULL,
      .pull        = GPIO_PULL_NONE,
      .speed       = GPIO_SPEED_HIGH,
      .alternate   = IMU2_SPI_MOSI_AF
  };

  /*
   * CS should start inactive/high before it becomes an output.
   */
  gpio_config_t imu2_cs = {
      .port        = IMU2_CS_PORT,
      .pin         = IMU2_CS_PIN,
      .mode        = GPIO_MODE_OUTPUT,
      .output_type = GPIO_PUSH_PULL,
      .pull        = GPIO_PULL_NONE,
      .speed       = GPIO_SPEED_LOW,
      .alternate   = 0U,
      .init_state  = true
  };

  gpio_init(&imu1_sck);
  gpio_init(&imu1_miso);
  gpio_init(&imu1_mosi);
  gpio_init(&imu1_cs);

  gpio_init(&imu2_sck);
  gpio_init(&imu2_miso);
  gpio_init(&imu2_mosi);
  gpio_init(&imu2_cs);
}
