#pragma once

#include "stm32h7xx.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool usb_init(void);

void usb_console_write(const char *str);
bool usb_console_write_bytes(const char *data, size_t length);
void usb_console_putchar(const char c);
uint32_t usb_console_read(char *buffer, uint32_t len);
bool usb_console_connected();
void usb_task(void);
uint32_t usb_console_dropped(void);
