#pragma once

#include "stm32h7xx.h"

#include <stdbool.h>
#include <stdint.h>

bool usb_init(void);

void usb_console_write(const char *str);
uint32_t usb_console_read(char *buffer, uint32_t len);
bool usb_console_connected();
