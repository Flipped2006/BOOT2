#include <stdio.h>

#include "stm32f4xx.h"

#include "console.h"
#include "bl_usart.h"
#include "time_delay.h"
#include "key.h"
#include "key_desc.h"
#include "led.h"
#include "led_desc.h"


// KEY1: PA0
// KEY2: PC4
// KEY3: PC5
static struct key_desc _key1 = {
    .port = GPIOA, .pin = GPIO_Pin_0, .active_level = Bit_RESET
};
static struct key_desc _key2 = {
    .port = GPIOC, .pin = GPIO_Pin_4, .active_level = Bit_RESET
};
static struct key_desc _key3 = {
    .port = GPIOC, .pin = GPIO_Pin_5, .active_level = Bit_RESET
};
key_desc_t key1 = &_key1;
key_desc_t key2 = &_key2;
key_desc_t key3 = &_key3;

// GPIOE, GPIO_Pin_9
// GPIOB, GPIO_Pin_0
// GPIOB, GPIO_Pin_1
static struct led_desc _led1 = {
    .port = GPIOE, .pin = GPIO_Pin_9, .onBit = Bit_RESET, .offBit = Bit_SET
};
static struct led_desc _led2 = {
    .port = GPIOB, .pin = GPIO_Pin_0, .onBit = Bit_RESET, .offBit = Bit_SET
};
static struct led_desc _led3 = {
    .port = GPIOB, .pin = GPIO_Pin_1, .onBit = Bit_RESET, .offBit = Bit_SET
};
led_desc_t led1 = &_led1;
led_desc_t led2 = &_led2;
led_desc_t led3 = &_led3;


void board_lowlevel_init(void)
{
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, ENABLE);
}

void board_init(void)
{
    console_init();
    time_init();
}

int fputc(int c, FILE *fp)
{
    USART_SendData(USART1, (uint8_t)c);
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    return c;
}

