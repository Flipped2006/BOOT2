#include "stm32f4xx.h"
#include "stm32f4xx_ll_dma.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_usart.h"

#include "bl_usart.h"

static bl_usart_rx_callback_t bl_usart_rx_callback = 0;

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
}

static void bl_usart_it_init(void)
{
    LL_USART_ClearFlag_RXNE(USART3);
    LL_DMA_ClearFlag_TC3(DMA1);
    LL_DMA_ClearFlag_TC1(DMA1);

    NVIC_EnableIRQ(USART3_IRQn);
    NVIC_SetPriority(USART3_IRQn, 5);

//    DMA_ITConfig(DMA1_Stream1 | DMA1_Stream3, DMA_IT_TC, ENABLE);
    LL_USART_EnableIT_RXNE(USART3);
}

// void bl_usart_dma_init(void)
// {
    // // TX DMA1_Stream3_Channel4
    // DMA_InitTypeDef DMA_InitStructure;
    // DMA_StructInit(&DMA_InitStructure);
    // DMA_InitStructure.DMA_Channel = DMA_Channel_4;
    // DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&(USART3->DR);
    // DMA_InitStructure.DMA_DIR = DMA_DIR_MemoryToPeripheral;
    // DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    // DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    // DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    // DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
    // DMA_InitStructure.DMA_Priority = DMA_Priority_Low;
    // DMA_InitStructure.DMA_FIFOMode = DMA_FIFOMode_Disable;
    // DMA_InitStructure.DMA_FIFOThreshold = DMA_FIFOThreshold_HalfFull;
    // DMA_InitStructure.DMA_MemoryBurst = DMA_MemoryBurst_Single;
    // DMA_InitStructure.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
    // DMA_InitStructure.DMA_Memory0BaseAddr = (uint32_t)0;
    // DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    // DMA_InitStructure.DMA_BufferSize = 0;
    // DMA_Init(DMA1_Stream3, &DMA_InitStructure);
    // DMA_ITConfig(DMA1_Stream3, DMA_IT_TC, ENABLE);

//     // RX DMA1_Stream1_Channel4
//     DMA_StructInit(&DMA_InitStructure);
//     DMA_InitStructure.DMA_Channel = DMA_Channel_4;
//     DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&(USART3->DR);
//     DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralToMemory;
//     DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
//     DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Disable;
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
//     DMA_Init(DMA1_Stream1, &DMA_InitStructure);
//     USART_DMACmd(USART3, USART_DMAReq_Tx | USART_DMAReq_Rx, ENABLE);
// }

void bl_usart_init(void)
{
    bl_usart_io_init();
    bl_usart_lowlevel_init();
    bl_usart_it_init();
}

void bl_usart_write(uint8_t* data, uint16_t len)
{
    while (len--)
    {
        LL_USART_TransmitData8(USART3, *data++);
        while (LL_USART_IsActiveFlag_TC(USART3) == 0);
    }

    // uint32_t write_chunk = len < 65535 ? len : 65535;m
    // do
    // {
    //     DMA1_Stream3->NDTR = write_chunk;
    //     DMA1_Stream3->M0AR = (uint32_t)data;
    //     DMA_Cmd(DMA1_Stream3, ENABLE);
    //     while (DMA_GetFlagStatus(DMA1_Stream3, DMA_FLAG_TCIF3) == RESET);
    //     data += write_chunk;
    //     len -= write_chunk;
    // } while (len > 0);
}

void bl_usart_rx_callback_register(bl_usart_rx_callback_t callback)
{
    bl_usart_rx_callback = callback;
}

void USART3_IRQHandler(void)
{
    if (LL_USART_IsActiveFlag_RXNE(USART3) != 0)
    {
        LL_USART_ClearFlag_RXNE(USART3);
        uint8_t data = LL_USART_ReceiveData8(USART3);
        if (bl_usart_rx_callback)
            bl_usart_rx_callback(&data, 1);
    }
}
