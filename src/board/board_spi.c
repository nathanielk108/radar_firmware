#include "board/board_spi.h"
#include "board/board_gpio.h"
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
    .io_swap = true,
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
  board_gpio_pin(IMU1_SPI_SCK_PORT, IMU1_SPI_SCK_PIN, LL_GPIO_MODE_ALTERNATE,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_HIGH, IMU1_SPI_SCK_AF, false);

  board_gpio_pin(IMU1_SPI_MISO_PORT, IMU1_SPI_MISO_PIN, LL_GPIO_MODE_ALTERNATE,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_HIGH, IMU1_SPI_MISO_AF, false);

  board_gpio_pin(IMU1_SPI_MOSI_PORT, IMU1_SPI_MOSI_PIN, LL_GPIO_MODE_ALTERNATE,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_HIGH, IMU1_SPI_MOSI_AF, false);

  /*
   * CS should start inactive/high before it becomes an output.
   */
  board_gpio_pin(IMU1_CS_PORT, IMU1_CS_PIN, LL_GPIO_MODE_OUTPUT,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_LOW, 0U, true);
}

void board_spi_imu2_init(void) {
  board_gpio_pin(IMU2_SPI_SCK_PORT, IMU2_SPI_SCK_PIN, LL_GPIO_MODE_ALTERNATE,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_HIGH, IMU2_SPI_SCK_AF, false);

  board_gpio_pin(IMU2_SPI_MISO_PORT, IMU2_SPI_MISO_PIN, LL_GPIO_MODE_ALTERNATE,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_HIGH, IMU2_SPI_MISO_AF, false);

  board_gpio_pin(IMU2_SPI_MOSI_PORT, IMU2_SPI_MOSI_PIN, LL_GPIO_MODE_ALTERNATE,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_HIGH, IMU2_SPI_MOSI_AF, false);

  /*
   * CS should start inactive/high before it becomes an output.
   */
  board_gpio_pin(IMU2_CS_PORT, IMU2_CS_PIN, LL_GPIO_MODE_OUTPUT,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_LOW, 0U, true);
}

void board_spi_adc_init(void) {
  board_gpio_pin(ADC_A_SCK_PORT, ADC_A_SCK_PIN, LL_GPIO_MODE_ALTERNATE,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_HIGH, ADC_A_SCK_AF, false);

  board_gpio_pin(ADC_A_MISO_PORT, ADC_A_MISO_PIN, LL_GPIO_MODE_ALTERNATE,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_HIGH, ADC_A_MISO_AF, false);

  board_gpio_pin(ADC_A_MOSI_PORT, ADC_A_MOSI_PIN, LL_GPIO_MODE_ALTERNATE,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_HIGH, ADC_A_MOSI_AF, false);

  board_gpio_pin(ADC_A_CS_PORT, ADC_A_CS_PIN, LL_GPIO_MODE_OUTPUT,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_VERY_HIGH, 0U, true);

  board_gpio_pin(ADC_B_SCK_PORT, ADC_B_SCK_PIN, LL_GPIO_MODE_ALTERNATE,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_HIGH, ADC_B_SCK_AF, false);

  board_gpio_pin(ADC_B_MOSI_PORT, ADC_B_MOSI_PIN, LL_GPIO_MODE_ALTERNATE,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_HIGH, ADC_B_MOSI_AF, false);

  board_gpio_pin(ADC_B_CS_PORT, ADC_B_CS_PIN, LL_GPIO_MODE_ALTERNATE,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_LOW, ADC_B_CS_AF, false);
}
