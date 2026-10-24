#include <stdbool.h>

#include "stm32f4xx.h"
#include "crc32.h"
#include "utils.h"

#include "magic_header.h"

#define MAGIC_HEADER_ADDR 0x0800C000
#define MAGIC_HEADER 0x4D414749


// 256bytes = 128(version) + 128(magic info + fireware info)
typedef  struct {
    uint32_t magic;
    uint32_t bitmask;
    uint32_t reserve1[6];

    uint32_t data_type;
    uint32_t data_offset;
    uint32_t data_addr;
    uint32_t data_size;
    uint32_t data_crc;
    uint32_t reserve2[11];

    char version[128];

    uint32_t reserve3[6];
    uint32_t this_addr;
    uint32_t this_crc;
} magic_header_t;

bool magic_header_valid_check(void) {
    magic_header_t *header = (magic_header_t *)MAGIC_HEADER_ADDR;

    if (header->magic != MAGIC_HEADER) {
        return false;
    }

    int crc = header->this_crc;
    int ccrc = crc32((uint8_t *)header, OFFSET_OF(magic_header_t, this_crc));
    if (crc != ccrc) {
        return false;
    }
    return true;
}

magic_header_type_t magic_header_get_type(void) {
    magic_header_t *header = (magic_header_t *)MAGIC_HEADER_ADDR;
    return (magic_header_type_t)header->data_type;
}

uint32_t magic_header_get_offset(void) {
    magic_header_t *header = (magic_header_t *)MAGIC_HEADER_ADDR;
    return header->data_offset;
}

uint32_t magic_header_get_addr(void) {
    magic_header_t *header = (magic_header_t *)MAGIC_HEADER_ADDR;
    return header->data_addr;
}

uint32_t magic_header_get_size(void) {
    magic_header_t *header = (magic_header_t *)MAGIC_HEADER_ADDR;
    return header->data_size;
}

uint32_t magic_header_get_crc(void) {
    magic_header_t *header = (magic_header_t *)MAGIC_HEADER_ADDR;
    return header->data_crc;
}

char *magic_header_get_version(void) {
    magic_header_t *header = (magic_header_t *)MAGIC_HEADER_ADDR;
    return header->version;
}
