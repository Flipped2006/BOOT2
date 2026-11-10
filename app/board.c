#include <stdio.h>

#include "stm32f4xx.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_usart.h"
#include "stm32f4xx_ll_pwr.h"
#include "stm32f4xx_ll_system.h"
#include "stm32f4xx_ll_rcc.h"
#include "stm32f4xx_hal_cortex.h"

#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS.h"
#include "task.h"
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
    .port = GPIOA, .pin = LL_GPIO_PIN_0, .active_level = Bit_RESET
};
static struct key_desc _key2 = {
    .port = GPIOC, .pin = LL_GPIO_PIN_4, .active_level = Bit_RESET
};
static struct key_desc _key3 = {
    .port = GPIOC, .pin = LL_GPIO_PIN_5, .active_level = Bit_RESET
};
key_desc_t key1 = &_key1;
key_desc_t key2 = &_key2;
key_desc_t key3 = &_key3;

// GPIOE, LL_GPIO_PIN_9
// GPIOB, LL_GPIO_PIN_0
// GPIOB, LL_GPIO_PIN_1
static struct led_desc _led1 = {
    .port = GPIOE, .pin = LL_GPIO_PIN_9, .onBit = Bit_RESET, .offBit = Bit_SET
};
static struct led_desc _led2 = {
    .port = GPIOB, .pin = LL_GPIO_PIN_0, .onBit = Bit_RESET, .offBit = Bit_SET
};
static struct led_desc _led3 = {
    .port = GPIOB, .pin = LL_GPIO_PIN_1, .onBit = Bit_RESET, .offBit = Bit_SET
};
led_desc_t led1 = &_led1;
led_desc_t led2 = &_led2;
led_desc_t led3 = &_led3;


static void SystemClock_Config(void)
{
    /* Enable PWR clock and set voltage scaling to Scale 1 (for 168 MHz) */
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_PWR);
    LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE1);

    /* Enable HSE and wait until ready */
    LL_RCC_HSE_Enable();
    while (!LL_RCC_HSE_IsReady());

    /* Set Flash latency to 5 wait states (required for 168 MHz, 3.3V) */
    LL_FLASH_SetLatency(LL_FLASH_LATENCY_5);

    /* Configure PLL: HSE/4 * 168 / 2 = 168 MHz, PLLQ=4 for 48 MHz */
    LL_RCC_PLL_ConfigDomain_SYS(LL_RCC_PLLSOURCE_HSE, LL_RCC_PLLM_DIV_4, 168, LL_RCC_PLLP_DIV_2);
    LL_RCC_PLL_ConfigDomain_48M(LL_RCC_PLLSOURCE_HSE, LL_RCC_PLLM_DIV_4, 168, 4);

    /* Enable PLL and wait until ready */
    LL_RCC_PLL_Enable();
    while (!LL_RCC_PLL_IsReady());

    /* Set AHB, APB1, APB2 prescalers */
    LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
    LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_4);
    LL_RCC_SetAPB2Prescaler(LL_RCC_APB2_DIV_2);

    /* Switch SYSCLK to PLL */
    LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL);
    while (LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL);

    /* Update SystemCoreClock variable */
    SystemCoreClockUpdate();
}

void board_lowlevel_init(void)
{
    NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);
    SystemClock_Config();

    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOB);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOC);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOD);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOE);
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_USART1);
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_USART3);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA2);
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM6);
}

void board_init(void)
{
    console_init();
    time_init();
}

int fputc(int c, FILE *fp)
{
    LL_USART_TransmitData8(USART1, (uint8_t)c);
    while (LL_USART_IsActiveFlag_TXE(USART1) == 0);
    return c;
}

void vAssertCalled(const char *file, int line)
{
    __disable_irq();
    printf("vAssertCalled: %s, line %d\n", file, line);
    while (1);
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    __disable_irq();
    printf("vApplicationStackOverflowHook: %s\n", pcTaskName);
    while (1);
}

void vApplicationMallocFailedHook(void)
{
    __disable_irq();
    printf("vApplicationMallocFailedHook\n");
    while (1);
}

