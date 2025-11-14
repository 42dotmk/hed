#ifndef ERRORS_H
#define ERRORS_H

/*
 * HED ERROR HANDLING SYSTEM
 * =========================
 *
 * Provides consistent error codes and handling across the codebase.
 *
 * Design Philosophy:
 * - Explicit error codes instead of magic numbers (-1, 0, NULL)
 * - Functions that can fail return error codes
 * - Error messages are set separately via ed_set_status_message()
 * - Success is always 0 (ED_OK) for consistency with POSIX conventions
 *
 * Usage Pattern:
 *   EdError err = some_operation();
 *   if (err != ED_OK) {
 *       // Handle error (message already set)
 *       return err;
 *   }
 */

typedef enum {
    /* Success */
    ED_OK = 0,

    /* Memory errors */
    ED_ERR_NOMEM,           /* Out of memory */
    ED_ERR_ALLOC_FAILED,    /* Memory allocation failed */

    /* File I/O errors */
    ED_ERR_FILE_NOT_FOUND,  /* File does not exist */
    ED_ERR_FILE_OPEN,       /* Cannot open file */
    ED_ERR_FILE_READ,       /* Error reading file */
    ED_ERR_FILE_WRITE,      /* Error writing file */
    ED_ERR_FILE_PERM,       /* Permission denied */

    /* Buffer errors */
    ED_ERR_BUFFER_FULL,     /* Maximum buffers reached */
    ED_ERR_BUFFER_INVALID,  /* Invalid buffer index/pointer */
    ED_ERR_BUFFER_READONLY, /* Buffer is read-only */
    ED_ERR_BUFFER_DIRTY,    /* Buffer has unsaved changes */
    ED_ERR_BUFFER_EMPTY,    /* Buffer has no content */

    /* Window errors */
    ED_ERR_WINDOW_FULL,     /* Maximum windows reached */
    ED_ERR_WINDOW_INVALID,  /* Invalid window reference */
    ED_ERR_WINDOW_LAST,     /* Cannot close last window */

    /* Input/validation errors */
    ED_ERR_INVALID_ARG,     /* Invalid argument */
    ED_ERR_INVALID_INDEX,   /* Index out of bounds */
    ED_ERR_INVALID_RANGE,   /* Invalid range specification */
    ED_ERR_INVALID_INPUT,   /* Invalid user input */

    /* Operation errors */
    ED_ERR_NOT_FOUND,       /* Item not found */
    ED_ERR_NOT_SUPPORTED,   /* Operation not supported */
    ED_ERR_NO_MATCH,        /* No match found (search, etc.) */
    ED_ERR_CANCELLED,       /* Operation cancelled by user */

    /* System errors */
    ED_ERR_SYSTEM,          /* System error (check errno) */
    ED_ERR_UNKNOWN          /* Unknown error */
} EdError;

/*
 * Convert error code to human-readable string.
 * Useful for debugging and logging.
 */
const char *ed_error_string(EdError err);

/*
 * Check if error code indicates success.
 * Returns 1 if successful, 0 if error.
 */
static inline int ed_error_ok(EdError err) {
    return err == ED_OK;
}

/*
 * Check if error code indicates failure.
 * Returns 1 if error, 0 if successful.
 */
static inline int ed_error_failed(EdError err) {
    return err != ED_OK;
}

#endif /* ERRORS_H */
