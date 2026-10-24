#ifndef __CONSOLE_H__
#define __CONSOLE_H__

#include <stddef.h>

void console_init(void);
void console_write(const char* str, size_t size);

#endif /* __CONSOLE_H__ */
