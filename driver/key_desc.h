#ifndef __KEY_DESC_H__
#define __KEY_DESC_H__

#include <stdint.h>
#include "stm32f4xx.h"

struct key_desc
{
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t active_level;
};

#endif /* __KEY_H__ */
