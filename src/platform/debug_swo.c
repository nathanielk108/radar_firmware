#include "platform/debug_swo.h"
#include "stm32h7xx.h"


#define ITM_UNLOCK (0xC5ACCE55U)

void debug_swo_init(void) {
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

  ITM->LAR = ITM_UNLOCK;
  ITM->TCR |= ITM_TCR_ITMENA_Msk | ITM_TCR_DWTENA_Msk;
  ITM->TER |= 1U;
}

bool debug_swo_putc(char c) {
  /* Never stall acquisition if a debugger is absent or the ITM FIFO is full. */
  if ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) == 0U ||
      (ITM->TCR & ITM_TCR_ITMENA_Msk) == 0U ||
      (ITM->TER & 1U) == 0U ||
      ITM->PORT[0].u32 == 0U) return false;
  ITM->PORT[0].u8 = (uint8_t)c;
  return true;
}

void debug_swo_write(const char *str) {
  if (str == 0) {
    return;
  }

  while (*str != '\0') {
    (void)debug_swo_putc(*str++);
  }
}

void debug_swo_hex32(uint32_t value) {
  static const char hex[] = "0123456789ABCDEF";

  debug_swo_write("0x");

  for (int shift = 28; shift >= 0; shift -= 4) {
    debug_swo_putc(hex[(value >> shift) & 0xFU]);
  }
}

