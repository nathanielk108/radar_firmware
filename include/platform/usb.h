#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define USB_CONFIGURED_CURRENT_MA 500U

bool usb_init(void);

bool usb_console_write_bytes(const char *data, size_t length);
uint32_t usb_console_read(char *buffer, uint32_t len);
bool usb_console_connected(void);
bool usb_configured_power_available(void);
void usb_task(void);
