#include "platform/mpu.h"

void mpu_init(void) {
  RCC->AHB2ENR |= RCC_AHB2ENR_SRAM1EN;
  (void)RCC->AHB2ENR;

  ARM_MPU_Disable();

  ARM_MPU_SetRegionEx(
    0U,
    0x30000000U,
    ARM_MPU_RASR(
      1U,              // Execute never
      ARM_MPU_AP_FULL, // Full access
      1U,              // TEX: normal memory
      1U,              // Shareable
      0U,              // Not cacheable
      0U,              // Not bufferable
      0U,              // All subregions enabled
      ARM_MPU_REGION_SIZE_64KB
    )
  );

  ARM_MPU_Enable(MPU_CTRL_PRIVDEFENA_Msk);
}
