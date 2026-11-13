#include "platform/usb.h"
#include "platform/debug_swo.h"

/* Select a single backend during timing-critical bring-up. */
#define NONE 0U
#define SWO 1U
#define USB 2U
#define BOTH 3U

void _putchar(char c)
{

  #if PRINTF_OUT == NONE
  return;
  #endif

  #if PRINTF_OUT == USB || PRINTF_OUT == BOTH
  if (usb_console_connected()) {
    usb_console_putchar(c);
  }
  #endif

  #if PRINTF_OUT == SWO || PRINTF_OUT == BOTH
  debug_swo_putc(c);
  #endif
}
