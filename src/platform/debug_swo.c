#include "platform/debug_swo.h"
#include "board/board_gpio.h"
#include "board/board_pins.h"

#include <string.h>

#define SWO_QUEUE_CAPACITY 1024U
#define SWO_POLL_BUDGET 128U
#define ITM_UNLOCK 0xC5ACCE55U

/* Foreground queue. A fatal fault may also write before halting. */
static char queue[SWO_QUEUE_CAPACITY];
static uint16_t head;
static uint16_t tail;

void debug_swo_init(void) {
  board_gpio_pin(DEBUG_SWO_PORT, DEBUG_SWO_PIN, LL_GPIO_MODE_ALTERNATE,
                 LL_GPIO_OUTPUT_PUSHPULL, LL_GPIO_PULL_NO,
                 LL_GPIO_SPEED_FREQ_HIGH, DEBUG_SWO_AF, false);
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  ITM->LAR = ITM_UNLOCK;
  ITM->TCR |= ITM_TCR_ITMENA_Msk | ITM_TCR_DWTENA_Msk;
  ITM->TER |= 1U;
}

bool debug_swo_write_bytes(const char *data, size_t length) {
  if (!data || length == 0U)
    return length == 0U;
  size_t used =
      (size_t)((head + SWO_QUEUE_CAPACITY - tail) % SWO_QUEUE_CAPACITY);
  if (length > SWO_QUEUE_CAPACITY - 1U - used)
    return false; /* Never emit a partial log message. */
  size_t first = SWO_QUEUE_CAPACITY - head;
  if (first > length)
    first = length;
  memcpy(&queue[head], data, first);
  if (length > first)
    memcpy(queue, data + first, length - first);
  head = (uint16_t)((head + length) % SWO_QUEUE_CAPACITY);
  return true;
}

void debug_swo_poll(void) {
  if (!(CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) ||
      !(ITM->TCR & ITM_TCR_ITMENA_Msk) || !(ITM->TER & 1U))
    return;
  unsigned n = SWO_POLL_BUDGET;
  while (tail != head && n-- && ITM->PORT[0].u32 != 0U) {
    ITM->PORT[0].u8 = (uint8_t)queue[tail];
    tail = (uint16_t)((tail + 1U) % SWO_QUEUE_CAPACITY);
  }
}

void debug_swo_flush_bounded(uint32_t iterations) {
  while (head != tail && iterations-- != 0U)
    debug_swo_poll();
}

void debug_swo_write(const char *str) {
  if (str)
    (void)debug_swo_write_bytes(str, strlen(str));
}

void debug_swo_hex32(uint32_t value) {
  char buf[10] = {'0', 'x'};
  static const char hex[] = "0123456789ABCDEF";
  for (unsigned i = 0; i < 8U; ++i) {
    buf[2U + i] = hex[(value >> ((7U - i) * 4U)) & 0xFU];
  }
  (void)debug_swo_write_bytes(buf, sizeof(buf));
}
