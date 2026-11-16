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

bool usb_init(void) {
  PWR->CR3 |= PWR_CR3_USB33DEN;
  uint32_t start = time_ms();
  while (!(PWR->CR3 & PWR_CR3_USB33RDY)) {
    if (time_expired(start, 100U))
      return false;
  }

  /* clock_init() enables the 48 MHz PLL1Q output. */
  MODIFY_REG(RCC->D2CCIP2R, RCC_D2CCIP2R_USBSEL_Msk, RCC_D2CCIP2R_USBSEL_0);
  board_usb_init();
  RCC->AHB1ENR |= RCC_AHB1ENR_USB2OTGFSEN;
  (void)RCC->AHB1ENR;
  NVIC_SetPriority(OTG_FS_IRQn, 5U);
  return true;
}

void OTG_FS_IRQHandler(void) { tusb_int_handler(0U, true); }

uint32_t tusb_time_millis_api(void) { return time_ms(); }

void tud_umount_cb(void) { tx_head = tx_tail = 0U; }

bool usb_console_write_bytes(const char *data, size_t length) {
  if (!data)
    return false;
  if (length == 0U)
    return true;
  /* Buffer early boot logs until the host opens CDC. */
  size_t used =
      (size_t)((tx_head + USB_QUEUE_CAPACITY - tx_tail) % USB_QUEUE_CAPACITY);
  if (length > USB_QUEUE_CAPACITY - 1U - used)
    return false;
  size_t first = USB_QUEUE_CAPACITY - tx_head;
  if (first > length)
    first = length;
  memcpy(&tx_queue[tx_head], data, first);
  if (length > first)
    memcpy(tx_queue, data + first, length - first);
  tx_head = (uint16_t)((tx_head + length) % USB_QUEUE_CAPACITY);
  return true;
}

void usb_task(void) {
  if (!board_usb_vbus_present()) {
    if (tud_inited())
      (void)tusb_deinit(0U);
    return;
  }

  if (!tud_inited()) {
    const tusb_rhport_init_t init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_FULL,
    };
    NVIC_ClearPendingIRQ(OTG_FS_IRQn);
    if (!tusb_init(0U, &init)) {
      (void)tusb_deinit(0U);
      return;
    }
  }

  tud_task();
  if (!tud_cdc_connected())
    return;
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
  if (!buffer || !len || !tud_inited())
    return 0U;
  return tud_cdc_read(buffer, len);
}

bool usb_console_connected(void) { return tud_inited() && tud_cdc_connected(); }

bool usb_configured_power_available(void) {
  return tud_inited() && tud_mounted() && !tud_suspended();
}
