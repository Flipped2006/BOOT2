#ifndef __BL_USART_H__
#define __BL_USART_H__

#include <stdint.h>

typedef void (*bl_usart_rx_callback_t)(uint8_t* data, uint16_t len);

void bl_usart_init(void);
void bl_usart_write(uint8_t* data, uint16_t len);
void bl_usart_rx_callback_register(bl_usart_rx_callback_t callback);


#endif /* __BL_USART_H__ */
