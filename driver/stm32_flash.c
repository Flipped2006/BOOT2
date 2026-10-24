#include <stdio.h>

#include "stm32f4xx.h"

#include "stm32_flash.h"
#include "console.h"

#define LOG_TAG    "stm32 flash"
#define LOG_LVL    ELOG_LVL_INFO
#include <elog.h>

#define BASE_ADDR 0x08000000

typedef struct
{
    uint16_t flash_sector;
    uint32_t flash_sector_size;
} stm32_flash_sector_t;

static const stm32_flash_sector_t flash_sectors[] = {
    {FLASH_Sector_0, 16 * 1024},
    {FLASH_Sector_1, 16 * 1024},
    {FLASH_Sector_2, 16 * 1024},
    {FLASH_Sector_3, 16 * 1024},
    {FLASH_Sector_4, 64 * 1024},
    {FLASH_Sector_5, 128 * 1024},
    {FLASH_Sector_6, 128 * 1024},
    {FLASH_Sector_7, 128 * 1024},
    {FLASH_Sector_8, 128 * 1024},
    {FLASH_Sector_9, 128 * 1024},
    {FLASH_Sector_10, 128 * 1024},
    {FLASH_Sector_11, 128 * 1024},
};

void stm32_flash_unlock(void)
{
    FLASH_Unlock();
}

void stm32_flash_lock(void)
{
    FLASH_Lock();
}

void stm32_flash_erase_sector(uint32_t address, uint32_t size)
{
    uint32_t addr = BASE_ADDR;

    for (uint16_t i = 0; i < sizeof(flash_sectors) / sizeof(flash_sectors[0]); i++)
    {
        if (addr < address + size && addr + flash_sectors[i].flash_sector_size > address)
        {
            log_i("erase address at %08X, sector=%d, size=%d", addr, i, flash_sectors[i].flash_sector_size);
            if (FLASH_EraseSector(flash_sectors[i].flash_sector, VoltageRange_3) != FLASH_COMPLETE)
            {
                log_e("Error erasing sector %d\n", i);
                return;
            }
        }
        addr += flash_sectors[i].flash_sector_size;
    }
}

void stm32_flash_program(uint32_t address, uint32_t size, uint8_t *data)
{
    for (uint16_t i = 0; i < size; i += 4)
    {
        if (FLASH_ProgramWord(address + i, *(uint32_t *)(data + i)) != FLASH_COMPLETE)
        {
            log_e("Error writing to flash at address 0x%08X", address + i);
            return;
        }
    }
}
