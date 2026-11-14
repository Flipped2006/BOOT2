#ifndef __CONSOLE_H__
#define __CONSOLE_H__

#include <stdint.h>

typedef void (*console_rx_callback_t)(uint8_t data);

void console_init(void);
void console_write(const char* str, uint32_t size);
void console_rx_callback_register(console_rx_callback_t callback);

#endif /* __CONSOLE_H__ */
