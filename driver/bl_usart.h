#ifndef __BL_USART_H__
#define __BL_USART_H__

#include <stdint.h>
#include <stdbool.h>

typedef void (*bl_usart_rx_callback_t)(uint8_t* data, uint16_t size);
typedef void (*bl_usart_rx_async_callback_t)(void);

void bl_usart_init(void);
/* Sticky receive activity since bl_usart_init(), even after RXNE was cleared. */
bool bl_usart_rx_seen(void);
void bl_usart_write(uint8_t* data, uint16_t size);
void bl_usart_rx_callback_register(bl_usart_rx_callback_t callback);
void bl_usart_rx_async_callback_register(bl_usart_rx_async_callback_t callback);
void bl_usart_start_rx_aync(uint8_t* data, uint16_t size);
void bl_usart_abort_rx(void);

#endif /* __BL_USART_H__ */
