#include <stddef.h>

#include "stm32f4xx.h"
#include "stm32f4xx_ll_rcc.h"
#include "stm32f4xx_ll_tim.h"

#include "time_delay.h"

// TIMER6
// T = 1ms, 1 TICK = 1us

static volatile uint64_t time_ticks = 0;
static time_delay_func_t time_delay_func = NULL;

void time_it_init(void)
{
    NVIC_EnableIRQ(TIM6_DAC_IRQn);
    NVIC_SetPriority(TIM6_DAC_IRQn, 5);
}

void time_lowlevel_init(void)
{
    LL_RCC_ClocksTypeDef RCC_Clocks;
    LL_RCC_GetSystemClocksFreq(&RCC_Clocks);
    uint16_t apb1_freq_mhz = RCC_Clocks.PCLK1_Frequency / 1000000 * 2;

    LL_TIM_InitTypeDef TIM_TimeBaseStructure;
    LL_TIM_StructInit(&TIM_TimeBaseStructure);
    TIM_TimeBaseStructure.Autoreload = 1000 - 1;
    TIM_TimeBaseStructure.Prescaler = apb1_freq_mhz - 1;
    TIM_TimeBaseStructure.ClockDivision = LL_TIM_CLOCKDIVISION_DIV1;
    TIM_TimeBaseStructure.CounterMode = LL_TIM_COUNTERMODE_UP;
    LL_TIM_Init(TIM6, &TIM_TimeBaseStructure);

    LL_TIM_EnableIT_UPDATE(TIM6);

    LL_TIM_EnableCounter(TIM6);
}

void time_init(void)
{
    time_it_init();
    time_lowlevel_init();
}

uint64_t time_get_now(void)
{
    uint64_t now, last_tick;
    do {
        last_tick = time_ticks;
        now = time_ticks + LL_TIM_GetCounter(TIM6);
    } while (last_tick != time_ticks);
    return now;
}

uint64_t time_get_us(void)
{
    return time_get_now();
}

uint64_t time_get_ms(void)
{
    return time_get_now() / 1000;
}

void time_delay_us(uint64_t us)
{
    uint64_t start = time_get_us();
    while (time_get_us() - start < us);
}

void time_delay_ms(uint64_t ms)
{
    uint64_t start = time_get_ms();
    while (time_get_ms() - start < ms);
}

void time_delay_func_register(time_delay_func_t func)
{
    time_delay_func = func;
}

void TIM6_DAC_IRQHandler(void)
{
    if (LL_TIM_IsActiveFlag_UPDATE(TIM6) != 0) {
        LL_TIM_ClearFlag_UPDATE(TIM6);
        time_ticks += 1000;
        if (time_delay_func)
            time_delay_func();
    }
}
