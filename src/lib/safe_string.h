#ifndef SAFE_STRING_H
#define SAFE_STRING_H

#include <stddef.h>

/*
 * SAFE STRING UTILITIES
 * =====================
 *
 * Provides bounds-checked string operations to prevent buffer overflows.
 * All functions guarantee null termination.
 *
 * Design Goals:
 * - Always null-terminate output buffers
 * - Never write beyond buffer boundaries
 * - Return meaningful status codes
 * - Clear, consistent API
 */

#include "errors.h"

/*
 * Safe string copy - always null terminates.
 * Returns ED_OK on success, ED_ERR_INVALID_ARG if dst is too small.
 *
 * Unlike strncpy:
 * - Always null-terminates
 * - Returns error if truncation would occur
 * - Doesn't pad with zeros
 *
 * @param dst Destination buffer
 * @param src Source string
 * @param dst_size Size of destination buffer (including space for null)
 * @return ED_OK if copied fully, ED_ERR_INVALID_ARG if truncated
 */
EdError safe_strcpy(char *dst, const char *src, size_t dst_size);

/*
 * Safe string concatenation - always null terminates.
 * Returns ED_OK on success, ED_ERR_INVALID_ARG if dst is too small.
 *
 * @param dst Destination buffer (must contain valid null-terminated string)
 * @param src Source string to append
 * @param dst_size Total size of destination buffer
 * @return ED_OK if appended fully, ED_ERR_INVALID_ARG if truncated
 */
EdError safe_strcat(char *dst, const char *src, size_t dst_size);

/*
 * Safe formatted string print - always null terminates.
 * Returns ED_OK on success, ED_ERR_INVALID_ARG if buffer too small.
 *
 * @param dst Destination buffer
 * @param dst_size Size of destination buffer
 * @param fmt Format string (printf-style)
 * @param ... Format arguments
 * @return ED_OK if formatted fully, ED_ERR_INVALID_ARG if truncated
 */
EdError safe_sprintf(char *dst, size_t dst_size, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/*
 * Safe bounds checking macro.
 * Validates that an index is within array bounds.
 *
 * Example:
 *   if (!BOUNDS_CHECK(index, array_size)) {
 *       return ED_ERR_INVALID_INDEX;
 *   }
 *
 * Note: Casts size to int to avoid sign-comparison warnings.
 * This is safe as we use int for indices and won't exceed INT_MAX elements.
 */
#define BOUNDS_CHECK(index, size) ((index) >= 0 && (index) < (int)(size))

/*
 * Safe pointer validation macro.
 * Checks if pointer is non-NULL.
 *
 * Example:
 *   if (!PTR_VALID(buffer)) {
 *       return ED_ERR_INVALID_ARG;
 *   }
 */
#define PTR_VALID(ptr) ((ptr) != NULL)

/*
 * Safe range validation macro.
 * Validates that start <= end and both are within bounds.
 *
 * Example:
 *   if (!RANGE_VALID(start, end, size)) {
 *       return ED_ERR_INVALID_RANGE;
 *   }
 */
#define RANGE_VALID(start, end, size) \
    ((start) >= 0 && (end) >= (start) && (end) <= (size))

#endif /* SAFE_STRING_H */
