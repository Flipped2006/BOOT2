#ifndef __MAGIC_HEADER_H__
#define __MAGIC_HEADER_H__

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    APP_MAGIC_HEADER = 0
} magic_header_type_t;

bool magic_header_valid_check(void);
magic_header_type_t magic_header_get_type(void);
uint32_t magic_header_get_offset(void);
uint32_t magic_header_get_addr(void);
uint32_t magic_header_get_size(void);
uint32_t magic_header_get_crc(void);
char *magic_header_get_version(void);

#endif /* __MAGIC_HEADER_H__ */
