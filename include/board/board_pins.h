#pragma once

#include "stm32h7xx.h"

#define RF_5V_EN_PORT GPIOB
#define RF_5V_EN_PIN 5U

#define RF_15V_EN_PORT GPIOC
#define RF_15V_EN_PIN 14U

#define EFUSE_PG_PORT GPIOG
#define EFUSE_PG_PIN  5U

#define BATT_PG_PORT GPIOG
#define BATT_PG_PIN 1U

#define BATT_CE_PORT GPIOF
#define BATT_CE_PIN 13U

#define BATT_INT_PORT GPIOG
#define BATT_INT_PIN 0U

#define FREQ_LE_PORT GPIOA
#define FREQ_LE_PIN 3U
