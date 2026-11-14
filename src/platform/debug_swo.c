#include "platform/debug_swo.h"
#include "stm32h7xx.h"
#include <string.h>

#define SWO_QUEUE_CAPACITY 1024U
#define SWO_POLL_BUDGET 128U
#define ITM_UNLOCK 0xC5ACCE55U

/* Single-producer foreground queue; never called from an ISR. */
static char queue[SWO_QUEUE_CAPACITY];
static uint16_t head;
static uint16_t tail;
static uint32_t dropped;

void debug_swo_init(void) {
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
  if (length > SWO_QUEUE_CAPACITY - 1U - used) {
    dropped += (uint32_t)length;
    return false; /* Never emit a partial log message. */
  }
  size_t first = SWO_QUEUE_CAPACITY - head;
  if (first > length)
    first = length;
  memcpy(&queue[head], data, first);
  if (length > first)
    memcpy(queue, data + first, length - first);
  head = (uint16_t)((head + length) % SWO_QUEUE_CAPACITY);
  return true;
}

bool debug_swo_putc(char c) { return debug_swo_write_bytes(&c, 1U); }

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

uint32_t debug_swo_dropped(void) { return dropped; }

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
