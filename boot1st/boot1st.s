/*
 * STM32F407 first-stage loader, entered by a hardware/system reset.
 * No C runtime, RAM variables or stack operations are used while copying.
 * BOOT2_IMAGE_PATH is supplied by the root Makefile.
 */

  .syntax unified
  .cpu cortex-m4
  .thumb

  .equ SCB_VTOR, 0xE000ED08

  .section .isr_vector, "a", %progbits
  .balign 512
  .global g_pfnVectors
  .type g_pfnVectors, %object
g_pfnVectors:
  .word _estack
  .word Reset_Handler
  .word Default_Handler             /* NMI */
  .word Default_Handler             /* HardFault */
  .word Default_Handler             /* MemManage */
  .word Default_Handler             /* BusFault */
  .word Default_Handler             /* UsageFault */
  .word 0, 0, 0, 0
  .word Default_Handler             /* SVCall */
  .word Default_Handler             /* DebugMon */
  .word 0
  .word Default_Handler             /* PendSV */
  .word Default_Handler             /* SysTick */
  .rept 82                          /* IRQ0 through IRQ81 (FPU) */
  .word Default_Handler
  .endr
  .size g_pfnVectors, . - g_pfnVectors

  .section .text.Reset_Handler, "ax", %progbits
  .balign 4
  .global Reset_Handler
  .type Reset_Handler, %function
  .thumb_func
Reset_Handler:
  cpsid i

  /* Copy the exact binary length, including initialized .data, to SRAM. */
  ldr r0, =__boot2_flash_start__
  ldr r1, =__boot2_ram_start__
  ldr r2, =__boot2_image_size__
.Lcopy_words:
  cmp r2, #4
  blo .Lcopy_bytes
  ldr r3, [r0], #4
  str r3, [r1], #4
  subs r2, r2, #4
  b .Lcopy_words
.Lcopy_bytes:
  cbz r2, .Lcheck_vectors
  ldrb r3, [r0], #1
  strb r3, [r1], #1
  subs r2, r2, #1
  b .Lcopy_bytes

.Lcheck_vectors:
  ldr r0, =__boot2_ram_start__
  ldr r1, [r0]                      /* BOOT2 initial MSP */
  ldr r2, [r0, #4]                  /* BOOT2 Reset_Handler (Thumb) */

  /* Accept an 8-byte-aligned stack inside SRAM, including its upper end. */
  tst r1, #7
  bne Default_Handler
  cmp r1, r0
  bls Default_Handler
  ldr r3, =_estack
  cmp r1, r3
  bhi Default_Handler

  /* Reject erased vectors and images linked to execute from Flash. */
  tst r2, #1
  beq Default_Handler
  bic r3, r2, #1
  cmp r3, r0
  blo Default_Handler
  ldr r4, =__boot2_ram_end__
  cmp r3, r4
  bhs Default_Handler

  /* Publish the complete RAM image before fetching instructions from it. */
  ldr r3, =SCB_VTOR
  str r0, [r3]
  dsb
  isb

  /* BOOT2's regular startup initializes .bss, clocks/FPU and the C runtime. */
  ldr r3, =Default_Handler
  mov lr, r3
  msr msp, r1
  cpsie i
  bx r2
  .size Reset_Handler, . - Reset_Handler
  .ltorg

  .section .text.Default_Handler, "ax", %progbits
  .balign 4
  .global Default_Handler
  .type Default_Handler, %function
  .thumb_func
Default_Handler:
  cpsid i
.Lhalt:
  b .Lhalt
  .size Default_Handler, . - Default_Handler

#ifndef BOOT2_IMAGE_PATH
#error "Build with the root Makefile to supply the RAM-linked BOOT2 binary"
#endif
  .section .boot2_image, "a", %progbits
  .balign 4
  .incbin BOOT2_IMAGE_PATH
