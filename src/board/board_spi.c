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

const spi_config_t adc_master_spi_config = {
  .spi = SPI5,
  .role = SPI_ROLE_MASTER,
  .communication = SPI_COMM_FULL_DUPLEX,
  .mode = SPI_MODE_2,
  .bit_order = SPI_MSB_FIRST,
  .baud_divider = SPI_DIV_16,
  .data_size = 16U,
  .software_nss = true,
};

const spi_config_t adc_slave_spi_config = {
  .spi = SPI4,
  .role = SPI_ROLE_SLAVE,
  .communication = SPI_COMM_RX_ONLY,
  .mode = SPI_MODE_2,
  .bit_order = SPI_MSB_FIRST,
  .baud_divider = SPI_DIV_16,
  .data_size = 16U,
  .software_nss = false,
};

void board_spi_imu1_init(void) {
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

  gpio_init(&imu1_sck);
  gpio_init(&imu1_miso);
  gpio_init(&imu1_mosi);
  gpio_init(&imu1_cs);
}

void board_spi_imu2_init(void) {
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

  gpio_init(&imu2_sck);
  gpio_init(&imu2_miso);
  gpio_init(&imu2_mosi);
  gpio_init(&imu2_cs);
}

void board_spi_adc_init(void) {
  gpio_config_t adc_a_sck = {
      .port        = ADC_A_SCK_PORT,
      .pin         = ADC_A_SCK_PIN,
      .mode        = GPIO_MODE_ALTERNATE,
      .output_type = GPIO_PUSH_PULL,
      .pull        = GPIO_PULL_NONE,
      .speed       = GPIO_SPEED_HIGH,
      .alternate   = ADC_A_SCK_AF
  };

  gpio_config_t adc_a_miso = {
      .port        = ADC_A_MISO_PORT,
      .pin         = ADC_A_MISO_PIN,
      .mode        = GPIO_MODE_ALTERNATE,
      .output_type = GPIO_PUSH_PULL,
      .pull        = GPIO_PULL_NONE,
      .speed       = GPIO_SPEED_HIGH,
      .alternate   = ADC_A_MISO_AF
  };

  gpio_config_t adc_a_mosi = {
      .port        = ADC_A_MOSI_PORT,
      .pin         = ADC_A_MOSI_PIN,
      .mode        = GPIO_MODE_ALTERNATE,
      .output_type = GPIO_PUSH_PULL,
      .pull        = GPIO_PULL_NONE,
      .speed       = GPIO_SPEED_HIGH,
      .alternate   = ADC_A_MOSI_AF
  };

  gpio_config_t adc_a_cs = {
      .port        = ADC_A_CS_PORT,
      .pin         = ADC_A_CS_PIN,
      .mode        = GPIO_MODE_OUTPUT,
      .output_type = GPIO_PUSH_PULL,
      .pull        = GPIO_PULL_NONE,
      .speed       = GPIO_SPEED_LOW,
      .alternate   = 0U,
      .init_state  = true
  };

  gpio_config_t adc_b_sck = {
      .port        = ADC_B_SCK_PORT,
      .pin         = ADC_B_SCK_PIN,
      .mode        = GPIO_MODE_ALTERNATE,
      .output_type = GPIO_PUSH_PULL,
      .pull        = GPIO_PULL_NONE,
      .speed       = GPIO_SPEED_HIGH,
      .alternate   = ADC_B_SCK_AF
  };

  gpio_config_t adc_b_mosi = {
      .port        = ADC_B_MOSI_PORT,
      .pin         = ADC_B_MOSI_PIN,
      .mode        = GPIO_MODE_ALTERNATE,
      .output_type = GPIO_PUSH_PULL,
      .pull        = GPIO_PULL_NONE,
      .speed       = GPIO_SPEED_HIGH,
      .alternate   = ADC_B_MOSI_AF
  };

  gpio_config_t adc_b_cs = {
      .port        = ADC_B_CS_PORT,
      .pin         = ADC_B_CS_PIN,
      .mode        = GPIO_MODE_ALTERNATE,
      .output_type = GPIO_PUSH_PULL,
      .pull        = GPIO_PULL_NONE,
      .speed       = GPIO_SPEED_LOW,
      .alternate   = ADC_B_CS_AF
  };

  gpio_init(&adc_a_cs);
  gpio_init(&adc_a_sck);
  gpio_init(&adc_a_mosi);
  gpio_init(&adc_a_miso);

  gpio_init(&adc_b_cs);
  gpio_init(&adc_b_sck);
  gpio_init(&adc_b_mosi);
}

void board_spi_init(void) {
  board_spi_imu1_init();
  board_spi_imu2_init();
  board_spi_adc_init();
}
