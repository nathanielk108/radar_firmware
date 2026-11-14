#include "platform/usb.h"
#include "board/board_usb.h"

#include "platform/time.h"
#include "stm32h7xx.h"
#include "tusb.h"
#include <string.h>

#define USB_QUEUE_CAPACITY 2048U

static char tx_queue[USB_QUEUE_CAPACITY];
static uint16_t tx_head;
static uint16_t tx_tail;
static uint32_t dropped;

/* SysTick is initialized before usb_init(). */
static bool wait_set(volatile uint32_t *reg, uint32_t mask,
                     uint32_t timeout_ms) {
  const uint32_t start = time_ms();
  while ((*reg & mask) != mask) {
    if (time_expired(start, timeout_ms))
      return false;
  }
  return true;
}

bool usb_init(void) {
  PWR->CR3 |= PWR_CR3_USB33DEN;
  if (!wait_set(&PWR->CR3, PWR_CR3_USB33RDY, 100U))
    return false;

  /* PLL1Q = 48 MHz, selected as OTG FS kernel clock. */
  RCC->PLLCFGR |= RCC_PLLCFGR_DIVQ1EN;
  MODIFY_REG(RCC->D2CCIP2R, RCC_D2CCIP2R_USBSEL_Msk, RCC_D2CCIP2R_USBSEL_0);
  board_usb_init();
  RCC->AHB1ENR |= RCC_AHB1ENR_USB2OTGFSEN;
  (void)RCC->AHB1ENR;
  NVIC_SetPriority(OTG_FS_IRQn, 5U);
  NVIC_EnableIRQ(OTG_FS_IRQn);

  tusb_rhport_init_t init = {.role = TUSB_ROLE_DEVICE,
                             .speed = TUSB_SPEED_FULL};
  return tusb_init(0U, &init);
}

void OTG_FS_IRQHandler(void) { tusb_int_handler(0U, true); }

bool usb_console_write_bytes(const char *data, size_t length) {
  if (!data || !tud_cdc_connected())
    return false;
  if (length == 0U)
    return true;
  size_t used =
      (size_t)((tx_head + USB_QUEUE_CAPACITY - tx_tail) % USB_QUEUE_CAPACITY);
  if (length > USB_QUEUE_CAPACITY - 1U - used) {
    dropped += (uint32_t)length;
    return false;
  }
  size_t first = USB_QUEUE_CAPACITY - tx_head;
  if (first > length)
    first = length;
  memcpy(&tx_queue[tx_head], data, first);
  if (length > first)
    memcpy(tx_queue, data + first, length - first);
  tx_head = (uint16_t)((tx_head + length) % USB_QUEUE_CAPACITY);
  return true;
}

void usb_console_putchar(char c) { (void)usb_console_write_bytes(&c, 1U); }

void usb_console_write(const char *text) {
  if (text)
    (void)usb_console_write_bytes(text, strlen(text));
}

void usb_task(void) {
  tud_task();
  if (!tud_cdc_connected()) {
    tx_head = tx_tail = 0U;
    return;
  }
  /* Copy contiguous chunks into TinyUSB's existing transmit FIFO. */
  size_t budget = 256U;
  while (tx_head != tx_tail && budget) {
    uint32_t available = tud_cdc_write_available();
    if (!available)
      break;
    size_t n = tx_head > tx_tail ? (size_t)(tx_head - tx_tail)
                                 : (size_t)(USB_QUEUE_CAPACITY - tx_tail);
    if (n > (size_t)available)
      n = available;
    if (n > budget)
      n = budget;
    uint32_t sent = tud_cdc_write(&tx_queue[tx_tail], (uint32_t)n);
    if (!sent)
      break;
    tx_tail = (uint16_t)((tx_tail + sent) % USB_QUEUE_CAPACITY);
    budget -= sent;
  }
  tud_cdc_write_flush();
}

uint32_t usb_console_read(char *buffer, uint32_t len) {
  if (!buffer || !len)
    return 0U;
  return tud_cdc_read(buffer, len);
}

bool usb_console_connected(void) { return tud_cdc_connected(); }
uint32_t usb_console_dropped(void) { return dropped; }
