#pragma once

#include "../buf/buffer.h"
#include "../lib/errors.h"

/*
 * Apply a sed expression to a buffer in-place.
 *
 * Parameters:
 *   buf      - Buffer to modify
 *   sed_expr - Sed expression (e.g., "s/foo/bar/g", "/^#/d")
 *
 * Returns:
 *   ED_OK on success
 *   ED_ERR_INVALID_ARG if buf or sed_expr is NULL/empty
 *   ED_ERR_BUFFER_READONLY if buffer is read-only
 *   ED_ERR_NOMEM if memory allocation fails
 *   ED_ERR_SYSTEM if sed execution fails
 *
 * Notes:
 *   - Buffer dirty flag is incremented on success
 *   - Cursor position is preserved (clamped to valid range)
 *   - Changes are immediate; use undo to revert
 *   - The entire buffer content is replaced with sed output
 */
EdError sed_apply_to_buffer(Buffer *buf, const char *sed_expr);
