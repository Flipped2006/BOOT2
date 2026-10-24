#include "stm32f4xx.h"
#include "stm32f4xx_ll_gpio.h"

#include "time_delay.h"
#include "led_desc.h"
#include "led.h"

void led_init(led_desc_t led)
{
    LL_GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.Pin = led->pin;
    GPIO_InitStructure.Mode = LL_GPIO_MODE_OUTPUT;
    GPIO_InitStructure.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    GPIO_InitStructure.Speed = LL_GPIO_SPEED_FREQ_MEDIUM;
    GPIO_InitStructure.Pull = LL_GPIO_PULL_NO;
    LL_GPIO_Init(led->port, &GPIO_InitStructure);

    if (led->offBit)
        LL_GPIO_SetOutputPin(led->port, led->pin);
    else
        LL_GPIO_ResetOutputPin(led->port, led->pin);
}

void led_on(led_desc_t led)
{
    if (led->onBit)
        LL_GPIO_SetOutputPin(led->port, led->pin);
    else
        LL_GPIO_ResetOutputPin(led->port, led->pin);
}

void led_off(led_desc_t led)
{
    if (led->offBit)
        LL_GPIO_SetOutputPin(led->port, led->pin);
    else
        LL_GPIO_ResetOutputPin(led->port, led->pin);
}

void led_toggle(led_desc_t led)
{
    LL_GPIO_TogglePin(led->port, led->pin);
}

void led_blink(led_desc_t led, uint32_t delay)
{
    led_on(led);
    time_delay_ms(delay);
    led_off(led);
    time_delay_ms(delay);
}
