#ifndef __RINGBUFFER_H__
#define __RINGBUFFER_H__

#include <stdbool.h>
#include <stdint.h>


typedef struct ringbuffer *rb_t;


rb_t rb_init(uint8_t *buffer, uint16_t size);
bool rb_is_empty(rb_t rb);
bool rb_is_full(rb_t rb);
bool rb_write(rb_t rb, uint8_t data);
bool rb_read(rb_t rb, uint8_t *data);
bool rb_writes(rb_t rb, uint8_t *data, uint16_t len);
uint16_t rb_reads(rb_t rb, uint8_t *data, uint16_t len);

#endif /* __RINGBUFFER_H__ */
