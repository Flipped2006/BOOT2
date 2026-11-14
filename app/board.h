#ifndef __BOARD_H__
#define __BOARD_H__

#include <stdbool.h>
#include <stdint.h>

#include "key.h"
#include "led.h"

extern key_desc_t key1;
extern key_desc_t key2;
extern key_desc_t key3;

extern led_desc_t led1;
extern led_desc_t led2;
extern led_desc_t led3;

void board_lowlevel_init(void);
void board_module_init(void);

#endif /* __BOARD_H__ */
