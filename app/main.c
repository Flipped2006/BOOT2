#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "stm32f4xx.h"
#include "elog.h"

extern void board_lowlevel_init(void);
extern void board_init(void);
extern void bootloader_main(void);


int main(void)
{
    board_lowlevel_init();
    board_init();

#ifdef ELOG_OUTPUT_ENABLE
    elog_init();
    elog_set_fmt(ELOG_LVL_ASSERT, ELOG_FMT_ALL);
    elog_set_fmt(ELOG_LVL_ERROR, ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME);
    elog_set_fmt(ELOG_LVL_WARN, ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME);
    elog_set_fmt(ELOG_LVL_INFO, ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME);
    elog_set_fmt(ELOG_LVL_DEBUG, ELOG_FMT_ALL & ~ELOG_FMT_FUNC);
    elog_set_fmt(ELOG_LVL_VERBOSE, ELOG_FMT_ALL & ~ELOG_FMT_FUNC);
    elog_start();
#endif

    bootloader_main();
}
