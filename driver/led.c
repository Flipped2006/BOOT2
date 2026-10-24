#include "stm32f4xx.h"

#include "time_delay.h"
#include "led_desc.h"
#include "led.h"

void led_init(led_desc_t led)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin = led->pin;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Medium_Speed;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(led->port, &GPIO_InitStructure);
    GPIO_WriteBit(led->port, led->pin, led->offBit);
}

void led_on(led_desc_t led)
{
    GPIO_WriteBit(led->port, led->pin, led->onBit);
}

void led_off(led_desc_t led)
{
    GPIO_WriteBit(led->port, led->pin, led->offBit);
}

void led_toggle(led_desc_t led)
{
    GPIO_ToggleBits(led->port, led->pin);
}

void led_blink(led_desc_t led, uint32_t delay)
{
    led_on(led);
    time_delay_ms(delay);
    led_off(led);
    time_delay_ms(delay);
}
