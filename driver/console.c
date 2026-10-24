#include <stddef.h>

#include "stm32f4xx.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_usart.h"

#include "console.h"

// console
// USART1_TX: PA9
// DMA2_Stream7_Channel4
// USART1_RX: PA10
// DMA2_Stream2_Channel4

static void console_io_init(void)
{
    LL_GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.Pin = LL_GPIO_PIN_9 | LL_GPIO_PIN_10;
    GPIO_InitStructure.Mode = LL_GPIO_MODE_ALTERNATE;
    GPIO_InitStructure.Speed = LL_GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStructure.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    GPIO_InitStructure.Pull = LL_GPIO_PULL_UP;
    GPIO_InitStructure.Alternate = LL_GPIO_AF_7;
    LL_GPIO_Init(GPIOA, &GPIO_InitStructure);
}

static void console_lowlevel_init(void)
{
    LL_USART_InitTypeDef USART_InitStructure;
    USART_InitStructure.BaudRate = 115200;
    USART_InitStructure.DataWidth = LL_USART_DATAWIDTH_8B;
    USART_InitStructure.StopBits = LL_USART_STOPBITS_1;
    USART_InitStructure.Parity = LL_USART_PARITY_NONE;
    USART_InitStructure.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
    USART_InitStructure.TransferDirection = LL_USART_DIRECTION_TX_RX;
    USART_InitStructure.OverSampling = LL_USART_OVERSAMPLING_16;
    LL_USART_Init(USART1, &USART_InitStructure);

    LL_USART_Enable(USART1);
}

static void console_it_init(void)
{
    NVIC_EnableIRQ(USART1_IRQn);
    NVIC_SetPriority(USART1_IRQn, 5);
}

// void console_dma_init(void)
// {
//     DMA_InitTypeDef DMA_InitStructure;
//     DMA_StructInit(&DMA_InitStructure);
//     DMA_InitStructure.DMA_Channel = DMA_Channel_4;
//     DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&(USART1->DR);
//     DMA_InitStructure.DMA_DIR = DMA_DIR_MemoryToPeripheral;
//     DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
//     DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
//     DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
//     DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
//     DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
//     DMA_InitStructure.DMA_Priority = DMA_Priority_Low;
//     DMA_InitStructure.DMA_FIFOMode = DMA_FIFOMode_Disable;
//     DMA_InitStructure.DMA_FIFOThreshold = DMA_FIFOThreshold_HalfFull;
//     DMA_InitStructure.DMA_MemoryBurst = DMA_MemoryBurst_Single;
//     DMA_InitStructure.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
//     DMA_InitStructure.DMA_Memory0BaseAddr = (uint32_t)0;
//     DMA_InitStructure.DMA_BufferSize = 0;
//     DMA_Init(DMA2_Stream7, &DMA_InitStructure);
// }

void console_init(void)
{
    console_io_init();
    console_lowlevel_init();
    console_it_init();
}

void console_write(const char* str, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        while (LL_USART_IsActiveFlag_TXE(USART1) == 0)
            ;
        LL_USART_TransmitData8(USART1, str[i]);
    }
}
