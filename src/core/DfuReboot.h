#pragma once

// Jumper-free firmware update over the same USB cable.
//
// The BOOT0 jumper (J75) + RST dance is only needed to reach the STM32 ROM
// bootloader. We can get there from software instead:
//
//   1. Host sends "DFU\n"  -> dfuReboot(): arm a backup register, reset the MCU.
//   2. After the reset, dfuCheckAndJump() (first line of setup()) sees the armed
//      register, clears it, and jumps to the ROM bootloader at 0x1FFF0000.
//   3. Board enumerates as a DFU device (0483:df11).
//   4. dfu-util -a 0 -s 0x08000000:leave -D firmware.bin  -> flashes and boots.
//
// The BOOT0 jumper still works and stays the recovery path for a bricked image.

void dfuCheckAndJump();
void dfuReboot();
