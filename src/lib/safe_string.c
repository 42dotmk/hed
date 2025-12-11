#include "safe_string.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/*
 * Helper: Find string length with a maximum bound.
 * Returns the length of the string or maxlen if no null terminator found.
 */
static size_t safe_strnlen(const char *s, size_t maxlen) {
    const char *end = memchr(s, '\0', maxlen);
    return end ? (size_t)(end - s) : maxlen;
}

EdError safe_strcpy(char *dst, const char *src, size_t dst_size) {
    if (!PTR_VALID(dst) || !PTR_VALID(src) || dst_size == 0) {
        return ED_ERR_INVALID_ARG;
    }

    size_t src_len = strlen(src);

    /* Check if source fits in destination (including null terminator) */
    if (src_len >= dst_size) {
        /* Truncate: copy what fits and null-terminate */
        memcpy(dst, src, dst_size - 1);
        dst[dst_size - 1] = '\0';
        return ED_ERR_INVALID_ARG; /* Signal truncation */
    }

    /* Safe to copy everything */
    memcpy(dst, src, src_len + 1); /* Include null terminator */
    return ED_OK;
}

EdError safe_strcat(char *dst, const char *src, size_t dst_size) {
    if (!PTR_VALID(dst) || !PTR_VALID(src) || dst_size == 0) {
        return ED_ERR_INVALID_ARG;
    }

    size_t dst_len = safe_strnlen(dst, dst_size);

    /* Check if dst is already not null-terminated */
    if (dst_len == dst_size) {
        return ED_ERR_INVALID_ARG;
    }

    size_t src_len = strlen(src);
    size_t available = dst_size - dst_len - 1; /* Space for null terminator */

    if (src_len > available) {
        /* Truncate: append what fits and null-terminate */
        memcpy(dst + dst_len, src, available);
        dst[dst_size - 1] = '\0';
        return ED_ERR_INVALID_ARG; /* Signal truncation */
    }

    /* Safe to append everything */
    memcpy(dst + dst_len, src, src_len + 1); /* Include null terminator */
    return ED_OK;
}

EdError safe_sprintf(char *dst, size_t dst_size, const char *fmt, ...) {
    if (!PTR_VALID(dst) || !PTR_VALID(fmt) || dst_size == 0) {
        return ED_ERR_INVALID_ARG;
    }

    va_list args;
    va_start(args, fmt);
    int result = vsnprintf(dst, dst_size, fmt, args);
    va_end(args);

    /* vsnprintf returns number of chars that would have been written */
    if (result < 0) {
        /* Encoding error */
        dst[0] = '\0';
        return ED_ERR_INVALID_ARG;
    }

    if ((size_t)result >= dst_size) {
        /* Output was truncated */
        return ED_ERR_INVALID_ARG;
    }

    return ED_OK;
}
