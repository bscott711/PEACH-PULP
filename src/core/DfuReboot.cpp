#include "core/DfuReboot.h"

#include <Arduino.h>
#include "stm32_def.h"
#include "backup.h" // enableBackupDomain / set/getBackupRegister (SrcWrapper)

// STM32F446 system-memory (ROM) bootloader entry point — AN2606.
#define SYSMEM_BASE 0x1FFF0000UL

// A backup register that survives NVIC_SystemReset and that the core does not
// use (DR0: µs timebase, DR1: RTC_BKP_INDEX, DR4/DR10: BL_HID). DR2 is free.
#define DFU_BKP_INDEX LL_RTC_BKP_DR2
#define DFU_BKP_ARMED 0x50464455UL // 'PFDU'

static void jumpToSysmem() {
  __disable_irq();
  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL = 0;

  HAL_RCC_DeInit();
  HAL_DeInit();

  for (uint32_t i = 0; i < 8; i++) {
    NVIC->ICER[i] = 0xFFFFFFFFUL;
    NVIC->ICPR[i] = 0xFFFFFFFFUL;
  }

  __HAL_RCC_SYSCFG_CLK_ENABLE();
  __HAL_SYSCFG_REMAPMEMORY_SYSTEMFLASH(); // map ROM bootloader to 0x00000000
  __enable_irq();

  const uint32_t *v = (const uint32_t *)SYSMEM_BASE;
  __set_MSP(v[0]);
  ((void (*)(void))v[1])();
  for (;;) {
  }
}

// Called first thing in setup(): USB CDC is not up yet, the scheduler is not
// running, so it is safe to tear everything down and jump.
void dfuCheckAndJump() {
  enableBackupDomain();
  if (getBackupRegister(DFU_BKP_INDEX) == DFU_BKP_ARMED) {
    setBackupRegister(DFU_BKP_INDEX, 0);
    jumpToSysmem();
  }
}

// Called from the "DFU" serial command (serial task context).
void dfuReboot() {
  enableBackupDomain();
  setBackupRegister(DFU_BKP_INDEX, DFU_BKP_ARMED);
  Serial.flush(); // push the "!EVENT dfu" ack out before we reset
  delay(20);
  NVIC_SystemReset();
  for (;;) {
  }
}
