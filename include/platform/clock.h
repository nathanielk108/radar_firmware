#pragma once

typedef enum {
  CLOCK_OK = 0,
  CLOCK_ERR_SILICON_REV,
  CLOCK_ERR_VOS,
  CLOCK_ERR_FLASH,
  CLOCK_ERR_HSE,
  CLOCK_ERR_PLL,
  CLOCK_ERR_SYSCLK,
} clock_status_t;

/* Requires DWT CYCCNT enabled before calling (pre-SysTick timeouts). */
clock_status_t clock_init(void);
