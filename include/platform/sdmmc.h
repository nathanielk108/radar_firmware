#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SDMMC_SECTOR_SIZE 512U

void sdmmc_socket_init(void);
bool sdmmc_present(void);
bool sdmmc_ready(void);
bool sdmmc_card_init(void);
void sdmmc_reset(void);
uint32_t sdmmc_sector_count(void);
bool sdmmc_sync(void);
bool sdmmc_read(uint32_t sector, uint32_t count, uint8_t *data);
bool sdmmc_write(uint32_t sector, uint32_t count, const uint8_t *data);
