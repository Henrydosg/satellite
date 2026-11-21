#pragma once
#include <stddef.h>
#include <string.h>
#include <math.h>
inline size_t strlcpy(char* dst, const char* src, size_t size) {
    size_t length = strlen(src);
    if (size) { size_t copied = length < size - 1 ? length : size - 1; memcpy(dst, src, copied); dst[copied] = '\0'; }
    return length;
}
inline void yield() {}
