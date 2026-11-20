#include "app/radar_run.h"
#include "platform/debug_swo.h"
#include "services/recording.h"
#include "stm32h7xx.h"
#include "system/console.h"
#include "system/firmware.h"

int main(void) {
  if (!firmware_init()) {
    debug_swo_flush_bounded(10000U);
    for (;;)
      __WFI();
  }

  for (;;) {
    firmware_poll();
    console_poll();
    radar_run_poll();
    recording_task();
  }
}
