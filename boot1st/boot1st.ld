
/* First-stage loader and the embedded, RAM-linked BOOT2 binary. */
ENTRY(Reset_Handler)

MEMORY
{
  /* Keep the magic header at 0x0800C000 and the application untouched. */
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 48K
  RAM (xrw)  : ORIGIN = 0x20000000, LENGTH = 128K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
__boot2_ram_start__ = ORIGIN(RAM);
__boot2_image_size__ = SIZEOF(.boot2_image);
__boot2_ram_end__ = __boot2_ram_start__ + __boot2_image_size__;

SECTIONS
{
  .isr_vector ORIGIN(FLASH) :
  {
    KEEP(*(.isr_vector))
  } >FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text .text.*)
    *(.rodata .rodata.*)
    . = ALIGN(4);
    _etext = .;
  } >FLASH

  /* The payload's addresses remain those assigned by stm32f407xx_ram.ld. */
  .boot2_image ALIGN(4) :
  {
    __boot2_flash_start__ = .;
    KEEP(*(.boot2_image))
    __boot2_flash_end__ = .;
  } >FLASH
}

ASSERT(SIZEOF(.isr_vector) == (16 + 82) * 4,
       "BOOT1 requires the complete STM32F407 vector table")
ASSERT((__boot2_ram_start__ & 0x1FF) == 0,
       "BOOT2 vector table must be 512-byte aligned")
ASSERT(__boot2_image_size__ >= (16 + 82) * 4,
       "BOOT2 image is missing its STM32F407 vector table")
ASSERT(__boot2_image_size__ <= LENGTH(RAM),
       "BOOT2 image does not fit in SRAM")
ASSERT(__boot2_flash_end__ <= ORIGIN(FLASH) + LENGTH(FLASH),
       "BOOT1 + BOOT2 overlap the magic header at 0x0800C000")


