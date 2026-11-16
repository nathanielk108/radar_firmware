#pragma once

typedef enum { LOG_LEVEL_ERROR = 0, LOG_LEVEL_INFO } log_level_t;

/* Foreground only. Formatting and output queues are not ISR-safe. */
void log_write(log_level_t level, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

#define LOG_ERROR(...) log_write(LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_INFO(...) log_write(LOG_LEVEL_INFO, __VA_ARGS__)
