#pragma once

/* Only the SD card stack uses HAL. Clocks and SysTick belong to the platform. */
#define HAL_SD_MODULE_ENABLED
#define USE_HAL_SD_REGISTER_CALLBACKS 0U
#define USE_SD_TRANSCEIVER 0U
#define SDMMC_SWDATATIMEOUT 1000U
#define SDMMC_DATATIMEOUT 12000000U
#define assert_param(expr) ((void)0U)

#include "stm32h7xx_hal_rcc.h"
#include "stm32h7xx_hal_sd.h"
