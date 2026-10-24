#include "stm32f4xx.h"

#include "bl_usart.h"

static bl_usart_rx_callback_t bl_usart_rx_callback = 0;

// update console
// USART3_TX: PB10
// DMA1_Stream3_Channel4
// USART3_RX: PB11
// DMA1_Stream1_Channel4

static void bl_usart_io_init(void)
{
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource10, GPIO_AF_USART3);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource11, GPIO_AF_USART3);

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10 | GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
}

static void bl_usart_lowlevel_init(void)
{
    USART_InitTypeDef USART_InitStructure;
    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART3, &USART_InitStructure);

    USART_Cmd(USART3, ENABLE);
}

static void bl_usart_it_init(void)
{
    USART_ClearITPendingBit(USART3, USART_IT_RXNE | USART_IT_TXE);
    DMA_ClearITPendingBit(DMA1_Stream3, DMA_IT_TCIF3);
    DMA_ClearITPendingBit(DMA1_Stream1, DMA_IT_TCIF1);

    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 4;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
    NVIC_SetPriority(USART3_IRQn, 5);

//    DMA_ITConfig(DMA1_Stream1 | DMA1_Stream3, DMA_IT_TC, ENABLE);
    USART_ITConfig(USART3, USART_IT_RXNE, ENABLE); 
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
        USART_SendData(USART3, *data++);
        while (USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET);
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
    if (USART_GetITStatus(USART3, USART_IT_RXNE) != RESET)
    {
        USART_ClearITPendingBit(USART3, USART_IT_RXNE);
        uint8_t data = USART_ReceiveData(USART3);
        if (bl_usart_rx_callback)
            bl_usart_rx_callback(&data, 1);
    }
}

