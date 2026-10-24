#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "ringbuffer.h"

struct ringbuffer
{
    uint16_t head;
    uint16_t tail;
    uint16_t size;
    uint8_t buffer[];
};

rb_t rb_init(uint8_t *buffer, uint16_t size)
{
    if (size < sizeof(struct ringbuffer) + 1)
    {
        return NULL;
    }
    rb_t rb = (rb_t)buffer;
    rb->head = 0;
    rb->tail = 0;
    rb->size = size - sizeof(struct ringbuffer);
    return rb;
}

static uint16_t inline next_head(rb_t rb)
{
    return (rb->head + 1 < rb->size ? rb->head + 1 : 0);
}

static uint16_t inline next_tail(rb_t rb)
{
    return (rb->tail + 1 < rb->size ? rb->tail + 1 : 0);
}

bool rb_is_empty(rb_t rb)
{
    return rb->head == rb->tail;
}

bool rb_is_full(rb_t rb)
{
    return next_head(rb) == rb->tail;
}

bool rb_write(rb_t rb, uint8_t data)
{
    if (rb_is_full(rb))
    {
        return false;
    }
    rb->buffer[rb->head] = data;
    rb->head = next_head(rb);
    return true;
}

bool rb_read(rb_t rb, uint8_t *data)
{
    if (rb_is_empty(rb))
    {
        return false;
    }
    *data = rb->buffer[rb->tail];
    rb->tail = next_tail(rb);
    return true;
}

bool rb_writes(rb_t rb, uint8_t *data, uint16_t len)
{
    while (len--)
    {
        if (!rb_write(rb, *data++))
        {
            return false;
        }
    }
    return true;
}

uint16_t rb_reads(rb_t rb, uint8_t *data, uint16_t len)
{
    uint16_t count = 0;
    while (len--)
    {
        if (!rb_read(rb,
            data++))
        {
            break;
        }
        count++;
    }
    return count;
}
