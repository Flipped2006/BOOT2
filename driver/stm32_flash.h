#ifndef __STM32_FLASH_H__
#define __STM32_FLASH_H__

#include <stdint.h>

#define STM32_FLASH_BASE_ADDR           0x08000000
#define STM32_FLASH_SIZE                0x80000

void stm32_flash_unlock(void);
void stm32_flash_lock(void);
void stm32_flash_erase_sector(uint32_t address, uint32_t size);
void stm32_flash_program(uint32_t address, uint32_t size, uint8_t *data);


#endif /* __STM32_FLASH_H__ */
