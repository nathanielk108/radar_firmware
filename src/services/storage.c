#include "services/storage.h"

#include "diskio.h"
#include "ff.h"
#include "platform/sdmmc.h"
#include <limits.h>

static FATFS filesystem;
static FIL file;
static bool mounted;
static bool file_open;

static void drop_mount(void) {
  (void)f_mount(NULL, "", 0U);
  sdmmc_reset();
  mounted = false;
  file_open = false;
}

static storage_status_t status_from_fatfs(FRESULT result) {
  if (result == FR_OK)
    return STORAGE_OK;
  if (!sdmmc_present()) {
    drop_mount();
    return STORAGE_NO_MEDIA;
  }
  switch (result) {
  case FR_NO_FILESYSTEM:
    return STORAGE_NO_FILESYSTEM;
  case FR_NO_FILE:
  case FR_NO_PATH:
    return STORAGE_NOT_FOUND;
  case FR_EXIST:
    return STORAGE_EXISTS;
  case FR_INVALID_NAME:
  case FR_INVALID_PARAMETER:
    return STORAGE_INVALID_ARGUMENT;
  case FR_DISK_ERR:
  case FR_INT_ERR:
  case FR_NOT_READY:
    drop_mount();
    return STORAGE_IO_ERROR;
  default:
    return STORAGE_IO_ERROR;
  }
}

void storage_init(void) { sdmmc_socket_init(); }

void storage_poll(void) {
  if (mounted && !sdmmc_ready())
    drop_mount();
}

bool storage_present(void) { return sdmmc_present(); }

bool storage_mounted(void) { return mounted && sdmmc_ready(); }

storage_status_t storage_mount(void) {
  storage_poll();
  if (!storage_present())
    return STORAGE_NO_MEDIA;
  if (mounted)
    return STORAGE_OK;
  storage_status_t status = status_from_fatfs(f_mount(&filesystem, "", 1U));
  if (status == STORAGE_OK)
    mounted = true;
  else
    drop_mount();
  return status;
}

storage_status_t storage_unmount(void) {
  storage_poll();
  if (file_open)
    return STORAGE_BUSY;
  drop_mount();
  return STORAGE_OK;
}

storage_status_t storage_open(const char *name, storage_open_mode_t mode) {
  static const BYTE modes[] = {FA_READ, FA_WRITE | FA_CREATE_NEW,
                               FA_WRITE | FA_OPEN_APPEND};
  if (!name || !*name || (unsigned)mode >= sizeof(modes) / sizeof(modes[0]))
    return STORAGE_INVALID_ARGUMENT;
  storage_poll();
  if (!mounted)
    return storage_present() ? STORAGE_NOT_MOUNTED : STORAGE_NO_MEDIA;
  if (file_open)
    return STORAGE_BUSY;
  storage_status_t status = status_from_fatfs(f_open(&file, name, modes[mode]));
  file_open = status == STORAGE_OK;
  return status;
}

static storage_status_t file_status(void) {
  storage_poll();
  if (!storage_present())
    return STORAGE_NO_MEDIA;
  return file_open ? STORAGE_OK : STORAGE_NOT_OPEN;
}

storage_status_t storage_read(void *data, size_t length, size_t *actual) {
  if (!data || !actual || length > UINT_MAX)
    return STORAGE_INVALID_ARGUMENT;
  *actual = 0U;
  storage_status_t status = file_status();
  if (status != STORAGE_OK)
    return status;
  UINT bytes = 0U;
  status = status_from_fatfs(f_read(&file, data, (UINT)length, &bytes));
  if (status == STORAGE_OK)
    *actual = bytes;
  return status;
}

storage_status_t storage_write(const void *data, size_t length) {
  if (!data || length > UINT_MAX)
    return STORAGE_INVALID_ARGUMENT;
  storage_status_t status = file_status();
  if (status != STORAGE_OK)
    return status;
  UINT bytes = 0U;
  status = status_from_fatfs(f_write(&file, data, (UINT)length, &bytes));
  if (status == STORAGE_OK && bytes != length)
    return STORAGE_NO_SPACE;
  return status;
}

storage_status_t storage_sync(void) {
  storage_status_t status = file_status();
  return status == STORAGE_OK ? status_from_fatfs(f_sync(&file)) : status;
}

storage_status_t storage_close(void) {
  storage_status_t status = file_status();
  if (status != STORAGE_OK)
    return status;
  status = status_from_fatfs(f_close(&file));
  file_open = false;
  return status;
}

const char *storage_status_name(storage_status_t status) {
  switch (status) {
  case STORAGE_OK:
    return "OK";
  case STORAGE_NO_MEDIA:
    return "no card";
  case STORAGE_NOT_MOUNTED:
    return "card not mounted";
  case STORAGE_NO_FILESYSTEM:
    return "no supported FAT filesystem";
  case STORAGE_NOT_OPEN:
    return "no open file";
  case STORAGE_BUSY:
    return "file already open";
  case STORAGE_NOT_FOUND:
    return "file not found";
  case STORAGE_EXISTS:
    return "file exists";
  case STORAGE_NO_SPACE:
    return "card or file full";
  case STORAGE_INVALID_ARGUMENT:
    return "invalid argument";
  default:
    return "card I/O error";
  }
}

DSTATUS disk_status(BYTE drive) {
  if (drive != 0U || !sdmmc_present())
    return STA_NOINIT | STA_NODISK;
  return sdmmc_ready() ? 0U : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE drive) {
  if (drive == 0U)
    (void)sdmmc_card_init();
  return disk_status(drive);
}

DRESULT disk_read(BYTE drive, BYTE *data, DWORD sector, UINT count) {
  if (drive != 0U || !data || !count)
    return RES_PARERR;
  if (!sdmmc_ready())
    return RES_NOTRDY;
  return sdmmc_read(sector, count, data) ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE drive, const BYTE *data, DWORD sector, UINT count) {
  if (drive != 0U || !data || !count)
    return RES_PARERR;
  if (!sdmmc_ready())
    return RES_NOTRDY;
  return sdmmc_write(sector, count, data) ? RES_OK : RES_ERROR;
}

DRESULT disk_ioctl(BYTE drive, BYTE command, void *data) {
  if (drive != 0U)
    return RES_PARERR;
  if (!sdmmc_ready())
    return RES_NOTRDY;
  switch (command) {
  case CTRL_SYNC:
    return sdmmc_sync() ? RES_OK : RES_ERROR;
  case GET_SECTOR_COUNT:
    if (!data)
      return RES_PARERR;
    *(DWORD *)data = sdmmc_sector_count();
    return RES_OK;
  case GET_SECTOR_SIZE:
    if (!data)
      return RES_PARERR;
    *(WORD *)data = SDMMC_SECTOR_SIZE;
    return RES_OK;
  default:
    return RES_PARERR;
  }
}
