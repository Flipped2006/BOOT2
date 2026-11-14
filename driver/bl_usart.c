#include "stm32f4xx.h"
#include "stm32f4xx_ll_dma.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_usart.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "bl_usart.h"

static bl_usart_rx_callback_t bl_usart_rx_callback;
static bl_usart_rx_async_callback_t bl_usart_rx_async_callback;
SemaphoreHandle_t bl_usart_tx_semaphore;

uint32_t baudrate = 2 * 1000 * 1000;

// update console
// USART3_TX: PB10
// DMA1_Stream3_Channel4
// USART3_RX: PB11
// DMA1_Stream1_Channel4

static void bl_usart_io_init(void)
{
    LL_GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.Pin = LL_GPIO_PIN_10 | LL_GPIO_PIN_11;
    GPIO_InitStructure.Mode = LL_GPIO_MODE_ALTERNATE;
    GPIO_InitStructure.Speed = LL_GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStructure.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    GPIO_InitStructure.Pull = LL_GPIO_PULL_UP;
    GPIO_InitStructure.Alternate = LL_GPIO_AF_7;
    LL_GPIO_Init(GPIOB, &GPIO_InitStructure);
}

static void bl_usart_lowlevel_init(void)
{
    LL_USART_InitTypeDef USART_InitStructure;
    USART_InitStructure.BaudRate = 2 * 1000 * 1000;
    USART_InitStructure.DataWidth = LL_USART_DATAWIDTH_8B;
    USART_InitStructure.StopBits = LL_USART_STOPBITS_1;
    USART_InitStructure.Parity = LL_USART_PARITY_NONE;
    USART_InitStructure.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
    USART_InitStructure.TransferDirection = LL_USART_DIRECTION_TX_RX;
    USART_InitStructure.OverSampling = LL_USART_OVERSAMPLING_16;
    LL_USART_Init(USART3, &USART_InitStructure);

    LL_USART_Enable(USART3);
    LL_USART_EnableDMAReq_TX(USART3);
    LL_USART_DisableDMAReq_RX(USART3);
}

static void bl_usart_it_init(void)
{
    LL_USART_ClearFlag_RXNE(USART3);
    LL_DMA_ClearFlag_TC3(DMA1);
    LL_DMA_ClearFlag_TC1(DMA1);

    NVIC_SetPriority(USART3_IRQn, 5);
    NVIC_EnableIRQ(USART3_IRQn);

    NVIC_SetPriority(DMA1_Stream3_IRQn, 5);
    NVIC_EnableIRQ(DMA1_Stream3_IRQn);

    NVIC_SetPriority(DMA1_Stream1_IRQn, 5);
    NVIC_EnableIRQ(DMA1_Stream1_IRQn);

    LL_USART_EnableIT_RXNE(USART3);
    LL_DMA_EnableIT_TC(DMA1, LL_DMA_STREAM_3);
    LL_DMA_EnableIT_TC(DMA1, LL_DMA_STREAM_1);
}

void bl_dma_init(void)
{
    LL_DMA_InitTypeDef DMA_InitStruct = {0};

    // TX
    LL_DMA_StructInit(&DMA_InitStruct);
    DMA_InitStruct.PeriphOrM2MSrcAddress  = (uint32_t)&(USART3->DR);
    DMA_InitStruct.MemoryOrM2MDstAddress  = 0;
    DMA_InitStruct.Direction              = LL_DMA_DIRECTION_MEMORY_TO_PERIPH;
    DMA_InitStruct.Mode                   = LL_DMA_MODE_NORMAL;
    DMA_InitStruct.PeriphOrM2MSrcIncMode  = LL_DMA_PERIPH_NOINCREMENT;
    DMA_InitStruct.MemoryOrM2MDstIncMode  = LL_DMA_MEMORY_INCREMENT;
    DMA_InitStruct.PeriphOrM2MSrcDataSize = LL_DMA_PDATAALIGN_BYTE;
    DMA_InitStruct.MemoryOrM2MDstDataSize = LL_DMA_MDATAALIGN_BYTE;
    DMA_InitStruct.NbData                 = 0;
    DMA_InitStruct.Channel                = LL_DMA_CHANNEL_4;
    DMA_InitStruct.Priority               = LL_DMA_PRIORITY_MEDIUM;
    DMA_InitStruct.FIFOMode               = LL_DMA_FIFOMODE_DISABLE;
    DMA_InitStruct.FIFOThreshold          = LL_DMA_FIFOTHRESHOLD_1_2;
    DMA_InitStruct.MemBurst               = LL_DMA_MBURST_SINGLE;
    DMA_InitStruct.PeriphBurst            = LL_DMA_PBURST_SINGLE;

    LL_DMA_Init(DMA1, LL_DMA_STREAM_3, &DMA_InitStruct);
    LL_DMA_DisableStream(DMA1, LL_DMA_STREAM_3);

    // RX
    DMA_InitStruct.Direction              = LL_DMA_DIRECTION_PERIPH_TO_MEMORY;

    LL_DMA_Init(DMA1, LL_DMA_STREAM_1, &DMA_InitStruct);
    LL_DMA_DisableStream(DMA1, LL_DMA_STREAM_1);
}

void bl_usart_init(void)
{
    bl_usart_tx_semaphore = xSemaphoreCreateBinary();
    configASSERT(bl_usart_tx_semaphore != NULL);

    bl_usart_io_init();
    bl_usart_lowlevel_init();
    bl_dma_init();
    bl_usart_it_init();
}

void bl_usart_write(uint8_t* data, uint16_t size)
{
    uint32_t len = size;
    do
    {
        uint32_t chunk_size = len > 65535 ? 65535 : len;
        LL_DMA_SetMemoryAddress(DMA1, LL_DMA_STREAM_3, (uint32_t)data);
        LL_DMA_SetDataLength(DMA1, LL_DMA_STREAM_3, chunk_size);
        LL_DMA_EnableStream(DMA1, LL_DMA_STREAM_3);
        xSemaphoreTake(bl_usart_tx_semaphore, pdMS_TO_TICKS(chunk_size * 1000 * 3 / (baudrate / 10)));

        data += chunk_size;
        len -= chunk_size;
    } while (len > 0);
    while (!LL_USART_IsActiveFlag_TC(USART3));
    LL_USART_ClearFlag_TC(USART3);
}

// version1: disable RXNE -> disable DMA -> clear all DMA flags -> enable DMA
void bl_usart_start_rx_aync(uint8_t* data, uint16_t size)
{
    configASSERT(data != NULL && size > 0);
    LL_USART_DisableIT_RXNE(USART3);
    LL_USART_DisableDMAReq_RX(USART3);
    while (LL_DMA_IsEnabledStream(DMA1, LL_DMA_STREAM_1));
    LL_DMA_ClearFlag_TC1(DMA1);
    LL_DMA_ClearFlag_HT1(DMA1);
    LL_DMA_ClearFlag_TE1(DMA1);
    LL_DMA_ClearFlag_DME1(DMA1);
    LL_DMA_ClearFlag_FE1(DMA1);
    LL_DMA_SetMemoryAddress(DMA1, LL_DMA_STREAM_1, (uint32_t)data);
    LL_DMA_SetDataLength(DMA1, LL_DMA_STREAM_1, size);
    LL_DMA_EnableStream(DMA1, LL_DMA_STREAM_1);
    LL_USART_EnableDMAReq_RX(USART3);
}

void bl_usart_rx_callback_register(bl_usart_rx_callback_t callback)
{
    bl_usart_rx_callback = callback;
}

void bl_usart_rx_async_callback_register(bl_usart_rx_async_callback_t callback)
{
    bl_usart_rx_async_callback = callback;
}

void USART3_IRQHandler(void)
{
    if (LL_USART_IsEnabledIT_RXNE(USART3) && LL_USART_IsActiveFlag_RXNE(USART3))
    {
        uint8_t data = LL_USART_ReceiveData8(USART3);
        if (bl_usart_rx_callback)
            bl_usart_rx_callback(&data, 1);
    }
}

// TX DMA + Semaphore against resource competition
void DMA1_Stream3_IRQHandler(void)
{
    if (LL_DMA_IsActiveFlag_TC3(DMA1))
    {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(bl_usart_tx_semaphore, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);

        LL_DMA_ClearFlag_TC3(DMA1);
        LL_DMA_DisableStream(DMA1, LL_DMA_STREAM_3);
    }
}

// RX DMA for huge data
void DMA1_Stream1_IRQHandler(void)
{
    if (LL_DMA_IsActiveFlag_TC1(DMA1))
    {
        LL_DMA_ClearFlag_TC1(DMA1);
        LL_DMA_DisableStream(DMA1, LL_DMA_STREAM_1);
        LL_USART_EnableIT_RXNE(USART3);
        if (bl_usart_rx_async_callback)
            bl_usart_rx_async_callback();
    }
}
