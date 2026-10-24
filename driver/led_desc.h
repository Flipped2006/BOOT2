#ifndef __LED_DESC_H__
#define __LED_DESC_H__

#include "stm32f4xx.h"


struct led_desc{
    GPIO_TypeDef* port;
    uint32_t pin;
    BitAction onBit;
    BitAction offBit;
};

#endif /* __LED_DESC_H__ */
