#ifndef __UTILS_H__
#define __UTILS_H__

#include "bitops.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define SIZEOF_ARRAY(arr) ARRAY_SIZE(arr)
#define OFFSET_OF(type, member) ((size_t) &((type *)0)->member)
#define CONTAINER_OF(ptr, type, member) ((type *)((char *)(ptr)-OFFSET_OF(type, member)))

#endif /* __UTILS_H__ */
