#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum {
  STORAGE_OK = 0,
  STORAGE_NO_MEDIA,
  STORAGE_NOT_MOUNTED,
  STORAGE_NO_FILESYSTEM,
  STORAGE_NOT_OPEN,
  STORAGE_BUSY,
  STORAGE_NOT_FOUND,
  STORAGE_EXISTS,
  STORAGE_NO_SPACE,
  STORAGE_INVALID_ARGUMENT,
  STORAGE_IO_ERROR
} storage_status_t;

typedef enum {
  STORAGE_OPEN_READ,
  STORAGE_OPEN_NEW,
  STORAGE_OPEN_APPEND
} storage_open_mode_t;

/* Foreground only. Mount and file I/O can wait for the card. Schedule those
 * calls outside a timing-critical capture. Poll is a bounded media check. */
void storage_init(void);
void storage_poll(void);
bool storage_present(void);
bool storage_mounted(void);
storage_status_t storage_mount(void);
storage_status_t storage_unmount(void);

/* One open file at a time on a preformatted FAT volume. Use 8.3 names.
 * The card can contain a partial file after an error or removal. */
storage_status_t storage_open(const char *name, storage_open_mode_t mode);
storage_status_t storage_read(void *data, size_t length, size_t *actual);
storage_status_t storage_write(const void *data, size_t length);
storage_status_t storage_sync(void);
storage_status_t storage_close(void);
const char *storage_status_name(storage_status_t status);
