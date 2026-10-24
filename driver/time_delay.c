#include <stddef.h>

#include "stm32f4xx.h"

#include "time_delay.h"

// TIMER6
// T = 1ms, 1 TICK = 1us

static volatile uint64_t time_ticks = 0;
static time_delay_func_t time_delay_func = NULL;

void time_it_init(void)
{
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = TIM6_DAC_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 5;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
    NVIC_SetPriority(TIM6_DAC_IRQn, 5);
}

void time_lowlevel_init(void)
{
    RCC_ClocksTypeDef RCC_Clocks;
    RCC_GetClocksFreq(&RCC_Clocks);
    uint16_t apb1_freq_mhz = RCC_Clocks.PCLK1_Frequency / 1000000 * 2;

    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_TimeBaseStructInit(&TIM_TimeBaseStructure);
    TIM_TimeBaseStructure.TIM_Period = 1000 - 1;
    TIM_TimeBaseStructure.TIM_Prescaler = apb1_freq_mhz - 1;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM6, &TIM_TimeBaseStructure);

    TIM_ITConfig(TIM6, TIM_IT_Update, ENABLE);

    TIM_Cmd(TIM6, ENABLE);
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
        now = time_ticks + TIM_GetCounter(TIM6);
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
    if (TIM_GetITStatus(TIM6, TIM_IT_Update) != RESET) {
        TIM_ClearITPendingBit(TIM6, TIM_IT_Update);
        time_ticks += 1000;
        if (time_delay_func)
            time_delay_func();
    }
}
