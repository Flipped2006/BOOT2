#include <stddef.h>

#include "stm32f4xx.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_usart.h"
#include "stm32f4xx_ll_dma.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "console.h"

static console_rx_callback_t console_rx_callback;
static SemaphoreHandle_t console_tx_semaphore;

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

    LL_USART_EnableIT_RXNE(USART1);
    LL_USART_EnableDMAReq_TX(USART1);
    LL_USART_Enable(USART1);
}

static void console_it_init(void)
{
    NVIC_SetPriority(USART1_IRQn, 5);
    NVIC_EnableIRQ(USART1_IRQn);

    NVIC_SetPriority(DMA2_Stream7_IRQn, 5);
    NVIC_EnableIRQ(DMA2_Stream7_IRQn);
}

void console_dma_init(void)
{
    LL_DMA_InitTypeDef DMA_InitStruct;
    LL_DMA_StructInit(&DMA_InitStruct);

    DMA_InitStruct.PeriphOrM2MSrcAddress  = (uint32_t)&(USART1->DR);
    DMA_InitStruct.MemoryOrM2MDstAddress  = 0;
    DMA_InitStruct.Direction              = LL_DMA_DIRECTION_MEMORY_TO_PERIPH;
    DMA_InitStruct.Mode                   = LL_DMA_MODE_NORMAL;
    DMA_InitStruct.PeriphOrM2MSrcIncMode  = LL_DMA_PERIPH_NOINCREMENT;
    DMA_InitStruct.MemoryOrM2MDstIncMode  = LL_DMA_MEMORY_INCREMENT;
    DMA_InitStruct.PeriphOrM2MSrcDataSize = LL_DMA_PDATAALIGN_BYTE;
    DMA_InitStruct.MemoryOrM2MDstDataSize = LL_DMA_MDATAALIGN_BYTE;
    DMA_InitStruct.NbData                 = 0;
    DMA_InitStruct.Channel                = LL_DMA_CHANNEL_4;
    DMA_InitStruct.Priority               = LL_DMA_PRIORITY_LOW;
    DMA_InitStruct.FIFOMode               = LL_DMA_FIFOMODE_DISABLE;
    DMA_InitStruct.FIFOThreshold          = LL_DMA_FIFOTHRESHOLD_1_2;
    DMA_InitStruct.MemBurst               = LL_DMA_MBURST_SINGLE;
    DMA_InitStruct.PeriphBurst            = LL_DMA_PBURST_SINGLE;

    LL_DMA_Init(DMA2, LL_DMA_STREAM_7, &DMA_InitStruct);
    LL_DMA_EnableIT_TC(DMA2, LL_DMA_STREAM_7);
    LL_DMA_DisableStream(DMA2, LL_DMA_STREAM_7);
}

void console_init(void)
{
    console_tx_semaphore = xSemaphoreCreateBinary();
    configASSERT(console_tx_semaphore != NULL);

    console_io_init();
    console_lowlevel_init();
    console_dma_init();
    console_it_init();
}

void console_write(const char* str, uint32_t size)
{
    if (xPortIsInsideInterrupt())
    {
        for (uint32_t i = 0; i < size; i++)
        {
            while (LL_USART_IsActiveFlag_TXE(USART1) == 0);
            LL_USART_TransmitData8(USART1, str[i]);
        }
    }
    else
    {
        uint32_t len = size;
        do {
            uint32_t chunk_size = len > 65535 ? 65535 : len;
            LL_DMA_SetMemoryAddress(DMA2, LL_DMA_STREAM_7, (uint32_t)str);
            LL_DMA_SetDataLength(DMA2, LL_DMA_STREAM_7, chunk_size);
            LL_DMA_EnableStream(DMA2, LL_DMA_STREAM_7);
            xSemaphoreTake(console_tx_semaphore, portMAX_DELAY);

            len -= chunk_size;
            str += chunk_size;
        } while (len > 0);
    }
    while (LL_USART_IsActiveFlag_TC(USART1) == 0);
    LL_USART_ClearFlag_TC(USART1);
}

void console_rx_callback_register(console_rx_callback_t callback)
{
    console_rx_callback = callback;
}

void DMA2_Stream7_IRQHandler(void)
{
    if (LL_DMA_IsActiveFlag_TC7(DMA2))
    {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(console_tx_semaphore, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);

        LL_DMA_ClearFlag_TC7(DMA2);
        LL_DMA_DisableStream(DMA2, LL_DMA_STREAM_7);
    }
}

void USART1_IRQHandler(void)
{
    if (LL_USART_IsActiveFlag_RXNE(USART1))
    {
        uint8_t data = LL_USART_ReceiveData8(USART1);
        if (console_rx_callback)
        {
            console_rx_callback(data);
        }
    }
}
