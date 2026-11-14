#include "system/log.h"
#include "platform/debug_swo.h"
#include "platform/time.h"
#include "platform/usb.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Retain the existing CMake PRINTF_OUT choice, now for message-based logs. */
#ifndef PRINTF_OUT
#define PRINTF_OUT 1
#endif

#define LOG_BUFFER_BYTES 384U
static log_level_t minimum_level = LOG_LEVEL_INFO;
static uint32_t truncated_count;

void log_set_level(log_level_t level) {
  if (level <= LOG_LEVEL_DEBUG)
    minimum_level = level;
}

uint32_t log_truncated_count(void) { return truncated_count; }

void log_write(log_level_t level, const char *format, ...) {
  if (!format || level > minimum_level)
    return;
  char message[LOG_BUFFER_BYTES];
  const char *tag = level == LOG_LEVEL_ERROR   ? "ERR"
                    : level == LOG_LEVEL_DEBUG ? "DBG"
                                               : "INF";
  int prefix = snprintf(message, sizeof(message), "[%lu] %s ",
                        (unsigned long)time_ms(), tag);
  if (prefix < 0 || (size_t)prefix >= sizeof(message) - 3U)
    return;

  va_list args;
  va_start(args, format);
  int needed = vsnprintf(message + prefix,
                         sizeof(message) - (size_t)prefix - 2U, format, args);
  va_end(args);
  if (needed < 0)
    return;

  size_t available = sizeof(message) - (size_t)prefix - 2U;
  size_t length = (size_t)needed;
  if (length >= available) {
    length = available - 1U;
    ++truncated_count;
  }
  length += (size_t)prefix;
  message[length++] = '\r';
  message[length++] = '\n';

#if PRINTF_OUT == 1 || PRINTF_OUT == 3
  (void)debug_swo_write_bytes(message, length);
#endif
#if PRINTF_OUT == 2 || PRINTF_OUT == 3
  (void)usb_console_write_bytes(message, length);
#endif
}

/* Compatibility with the existing vendor printf implementation.
   Use LOG_* for normal output: _putchar() cannot preserve message boundaries.
 */
void _putchar(char c) {
#if PRINTF_OUT == 1 || PRINTF_OUT == 3
  (void)debug_swo_putc(c);
#endif
#if PRINTF_OUT == 2 || PRINTF_OUT == 3
  usb_console_putchar(c);
#endif
#if PRINTF_OUT == 0
  (void)c;
#endif
}
