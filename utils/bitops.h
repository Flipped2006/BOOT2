#ifndef __BITOPS_H__
#define __BITOPS_H__

#include <stdint.h>
#include <string.h>

#define GET_VALUE_TEMPLATE(_NICK, _TYPE) \
static inline _TYPE get_##_NICK(uint8_t *ptr) \
{ \
    return *(_TYPE *)ptr; \
} \

#define GET_VALUE_INC_TEMPLATE(_NICK, _TYPE) \
static inline _TYPE get_##_NICK##_inc(uint8_t **ptr) \
{ \
    uint8_t *p = *ptr; \
    *ptr += sizeof(_TYPE); \
    return get_##_NICK(p); \
} \

#define PUT_VALUE_TEMPLATE(_NICK, _TYPE) \
static inline void put_##_NICK(uint8_t *ptr, _TYPE data) \
{ \
    *(_TYPE *)ptr = data; \
} \

#define PUT_VALUE_INC_TEMPLATE(_NICK, _TYPE) \
static inline void put_##_NICK##_inc(uint8_t **ptr, _TYPE data) \
{ \
    uint8_t *p = *ptr; \
    *ptr += sizeof(_TYPE); \
    put_##_NICK(p, data); \
} \

#define __VALUDE_OPERATION_TEMPLATE(_micro) \
_micro(u8, uint8_t) \
_micro(i8, int8_t) \
_micro(u16, uint16_t) \
_micro(i16, int16_t) \
_micro(u32, uint32_t) \
_micro(i32, int32_t) \
_micro(float, float) \
_micro(double, double)

__VALUDE_OPERATION_TEMPLATE(GET_VALUE_TEMPLATE)
__VALUDE_OPERATION_TEMPLATE(GET_VALUE_INC_TEMPLATE)
__VALUDE_OPERATION_TEMPLATE(PUT_VALUE_TEMPLATE)
__VALUDE_OPERATION_TEMPLATE(PUT_VALUE_INC_TEMPLATE)

static inline void put_bytes(uint8_t *ptr, uint8_t *data, uint32_t len)
{
    memcpy((void *)ptr, data, len);
}

static inline void put_bytes_inc(uint8_t **ptr, const uint8_t *data, uint32_t len)
{
    memcpy((void *)*ptr, data, len);
    *ptr += len;
}

static inline void get_bytes(uint8_t *ptr, uint8_t *data, uint32_t len)
{
    if ( ptr == NULL || data == NULL || len == 0)
        return;
    memcpy((void *)data, ptr, len);
}

static inline void get_bytes_inc(uint8_t **ptr, uint8_t *data, uint32_t len)
{
    if ( *ptr == NULL || data == NULL || len == 0)
        return;
    memcpy((void *)data, *ptr, len);
    *ptr += len;
}


#endif /* __BITOPS_H__ */
