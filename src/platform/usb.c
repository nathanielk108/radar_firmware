#include "platform/usb.h"
#include "board/board_usb.h"

#include "tusb.h"

#define CLOCK_TIMEOUT 2000000U

static bool wait_set(volatile uint32_t *reg, uint32_t mask) {
  for (uint32_t i = 0; i < CLOCK_TIMEOUT; ++i) {
    if ((*reg & mask) == mask) {
      return true;
    }
  }
  return false;
}

bool usb_init(void) {

  PWR->CR3 |= PWR_CR3_USB33DEN;

  if (!wait_set(&PWR->CR3, PWR_CR3_USB33RDY)) {
    return false;
  }

  if ((RCC->PLLCFGR & RCC_PLLCFGR_DIVQ1EN) == 0U) {
    RCC->PLLCFGR |= RCC_PLLCFGR_DIVQ1EN;
  }

  RCC->D2CCIP2R = (RCC->D2CCIP2R & ~RCC_D2CCIP2R_USBSEL_Msk) | RCC_D2CCIP2R_USBSEL_0;

  board_usb_init();

  RCC->AHB1ENR |= RCC_AHB1ENR_USB2OTGFSEN;
  (void)RCC->AHB1ENR;

  NVIC_SetPriority(OTG_FS_IRQn, 5);
  NVIC_EnableIRQ(OTG_FS_IRQn);

  tusb_rhport_init_t dev_init = {
    .role = TUSB_ROLE_DEVICE,
    .speed = TUSB_SPEED_FULL,
  };

  if (!tusb_init(0, &dev_init)) {
    return false;
  }

  return true;
}

void OTG_FS_IRQHandler(void) {
  tusb_int_handler(0, true);
}

void usb_console_putchar(const char c) {
  if (tud_cdc_connected() && tud_cdc_write_available() != 0U) {
    (void)tud_cdc_write(&c, 1U);
  }
}

void usb_task(void) {
  tud_task();
  if (tud_cdc_connected()) tud_cdc_write_flush();
}

void usb_console_write(const char *str) {
  if ((str == NULL) || !tud_cdc_connected()) return;

  while (*str != '\0') {
    uint32_t available = tud_cdc_write_available();
    if (available == 0U) break; /* Never wait indefinitely for USB host. */

    uint32_t length = 0U;
    while ((length < available) && (str[length] != '\0')) ++length;
    uint32_t written = tud_cdc_write(str, length);
    str += written;
    if (written < length) break;
  }
  tud_cdc_write_flush();
}

uint32_t usb_console_read(char *buffer, uint32_t len) {
  if (tud_cdc_available() > 0) {
    return tud_cdc_read(buffer, len);
  }
  else {
    return 0;
  }
}

bool usb_console_connected() {
  return tud_cdc_connected();
}

