#pragma once
#include <stdint.h>

typedef enum {
  LOG_LEVEL_ERROR = 0,
  LOG_LEVEL_INFO = 1,
  LOG_LEVEL_DEBUG = 2
} log_level_t;

/* Foreground only. Formatting and output queues are not ISR-safe. */
void log_write(log_level_t level, const char *format, ...);
void log_set_level(log_level_t level);
uint32_t log_truncated_count(void);

#define LOG_ERROR(...) log_write(LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_INFO(...) log_write(LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_DEBUG(...) log_write(LOG_LEVEL_DEBUG, __VA_ARGS__)
