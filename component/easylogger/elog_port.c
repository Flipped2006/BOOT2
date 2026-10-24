#include <stdio.h>
#include <stdint.h>

#include "console.h"
#include "time_delay.h"

#include "elog.h"

ElogErrCode elog_port_init(void)
{
    return ELOG_NO_ERR;
}

void elog_port_deinit(void)
{

}

void elog_port_output(const char *log, size_t size)
{
    console_write(log, size);
}

void elog_port_output_lock(void)
{

}

void elog_port_output_unlock(void)
{

}

const char *elog_port_get_time(void)
{
    static char tim_str[64];
    uint32_t ms = time_get_ms();
    uint32_t time_mm = ms / 1000 / 60;
    uint32_t time_ss = ms / 1000 % 60;
    uint32_t time_ms = ms % 1000;
    snprintf(tim_str, sizeof(tim_str), "%02ld:%02ld:%02ld", time_mm, time_ss, time_ms / 1000);
    return tim_str;
}

const char *elog_port_get_p_info(void)
{
    return "";
}

const char *elog_port_get_t_info(void)
{
    return "";
}
