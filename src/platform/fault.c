#include "platform/fault.h"
#include "platform/debug_swo.h"
#include "stm32h7xx.h"

#include <stdint.h>

typedef enum {
  FAULT_HARD = 0,
  FAULT_MEMMANAGE,
  FAULT_BUS,
  FAULT_USAGE,
} fault_type_t;

typedef struct {
  uint32_t r0;
  uint32_t r1;
  uint32_t r2;
  uint32_t r3;
  uint32_t r12;
  uint32_t lr;
  uint32_t pc;
  uint32_t xpsr;

  uint32_t cfsr;
  uint32_t hfsr;
  uint32_t mmfar;
  uint32_t bfar;
} fault_record_t;

volatile fault_record_t fault_record;

static void print_reg(const char *name, uint32_t value) {
  debug_swo_write(name);
  debug_swo_write(" = ");
  debug_swo_hex32(value);
  debug_swo_write("\r\n");
}

__attribute__((noreturn))
void fault_handler_c(uint32_t *stack, uint32_t type) {
  debug_swo_write("\r\n*** FAULT ***\r\n");

  switch ((fault_type_t)type) {
    case FAULT_HARD:
      debug_swo_write("HardFault\r\n");
      break;
    case FAULT_MEMMANAGE:
      debug_swo_write("MemManageFault\r\n");
      break;
    case FAULT_BUS:
      debug_swo_write("BusFault\r\n");
      break;
    case FAULT_USAGE:
      debug_swo_write("UsageFault\r\n");
      break;
  }

  /*
   * Cortex-M exception stack frame:
   *
   * stack[0] = R0
   * stack[1] = R1
   * stack[2] = R2
   * stack[3] = R3
   * stack[4] = R12
   * stack[5] = LR
   * stack[6] = PC
   * stack[7] = xPSR
   */

  fault_record.r0   = stack[0];
  fault_record.r1   = stack[1];
  fault_record.r2   = stack[2];
  fault_record.r3   = stack[3];
  fault_record.r12  = stack[4];
  fault_record.lr   = stack[5];
  fault_record.pc   = stack[6];
  fault_record.xpsr = stack[7];

  fault_record.cfsr  = SCB->CFSR;
  fault_record.hfsr  = SCB->HFSR;
  fault_record.mmfar = SCB->MMFAR;
  fault_record.bfar  = SCB->BFAR;

  print_reg("R0  ", stack[0]);
  print_reg("R1  ", stack[1]);
  print_reg("R2  ", stack[2]);
  print_reg("R3  ", stack[3]);
  print_reg("R12 ", stack[4]);
  print_reg("LR  ", stack[5]);
  print_reg("PC  ", stack[6]);
  print_reg("xPSR", stack[7]);

  debug_swo_write("\r\nSCB:\r\n");

  print_reg("CFSR ", SCB->CFSR);
  print_reg("HFSR ", SCB->HFSR);
  print_reg("DFSR ", SCB->DFSR);
  print_reg("MMFAR", SCB->MMFAR);
  print_reg("BFAR ", SCB->BFAR);
  print_reg("AFSR ", SCB->AFSR);
  print_reg("SHCSR", SCB->SHCSR);


  while ((ITM->TCR & ITM_TCR_BUSY_Msk) != 0U) {
    __NOP();
  }

  /*
   * Only execute BKPT when a debugger is actually attached.
   * Otherwise BKPT itself can generate another fault.
   */
  if ((CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0U)
  {
      __BKPT(0);
  }

  while (1)
  {
      __NOP();
  }
}

__attribute__((naked))
void HardFault_Handler(void)
{
    __asm volatile(
        "tst lr, #4        \n"
        "ite eq            \n"
        "mrseq r0, msp     \n"
        "mrsne r0, psp     \n"
        "movs r1, #0       \n"
        "b fault_handler_c \n"
    );
}

__attribute__((naked))
void MemManage_Handler(void)
{
    __asm volatile(
        "tst lr, #4        \n"
        "ite eq            \n"
        "mrseq r0, msp     \n"
        "mrsne r0, psp     \n"
        "movs r1, #1       \n"
        "b fault_handler_c \n"
    );
}

__attribute__((naked))
void BusFault_Handler(void)
{
    __asm volatile(
        "tst lr, #4        \n"
        "ite eq            \n"
        "mrseq r0, msp     \n"
        "mrsne r0, psp     \n"
        "movs r1, #2       \n"
        "b fault_handler_c \n"
    );
}

__attribute__((naked))
void UsageFault_Handler(void)
{
    __asm volatile(
        "tst lr, #4        \n"
        "ite eq            \n"
        "mrseq r0, msp     \n"
        "mrsne r0, psp     \n"
        "movs r1, #3       \n"
        "b fault_handler_c \n"
    );
}

void fault_test_usage(void) {
  __asm volatile("udf #0");
}
