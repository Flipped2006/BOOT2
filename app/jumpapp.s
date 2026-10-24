/**
  * @file    jumpapp.s
  * @brief   JumptoApp for GCC based toolchains.
  *          Load application initial SP and reset handler from its
  *          vector table, then jump (r0 = application base address).
  */

  .syntax unified
  .cpu cortex-m4
  .thumb

  .section  .text.JumptoApp, "ax", %progbits
  .align    2
  .global   JumptoApp
  .thumb_func
  .type     JumptoApp, %function
JumptoApp:
  ldr     sp, [r0, #0]
  ldr     pc, [r0, #4]
  .size   JumptoApp, . - JumptoApp
