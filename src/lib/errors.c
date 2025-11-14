#include "errors.h"

const char *ed_error_string(EdError err) {
    switch (err) {
        case ED_OK:
            return "Success";

        /* Memory errors */
        case ED_ERR_NOMEM:
            return "Out of memory";
        case ED_ERR_ALLOC_FAILED:
            return "Memory allocation failed";

        /* File I/O errors */
        case ED_ERR_FILE_NOT_FOUND:
            return "File not found";
        case ED_ERR_FILE_OPEN:
            return "Cannot open file";
        case ED_ERR_FILE_READ:
            return "Error reading file";
        case ED_ERR_FILE_WRITE:
            return "Error writing file";
        case ED_ERR_FILE_PERM:
            return "Permission denied";

        /* Buffer errors */
        case ED_ERR_BUFFER_FULL:
            return "Maximum buffers reached";
        case ED_ERR_BUFFER_INVALID:
            return "Invalid buffer";
        case ED_ERR_BUFFER_READONLY:
            return "Buffer is read-only";
        case ED_ERR_BUFFER_DIRTY:
            return "Buffer has unsaved changes";
        case ED_ERR_BUFFER_EMPTY:
            return "Buffer is empty";

        /* Window errors */
        case ED_ERR_WINDOW_FULL:
            return "Maximum windows reached";
        case ED_ERR_WINDOW_INVALID:
            return "Invalid window";
        case ED_ERR_WINDOW_LAST:
            return "Cannot close last window";

        /* Input/validation errors */
        case ED_ERR_INVALID_ARG:
            return "Invalid argument";
        case ED_ERR_INVALID_INDEX:
            return "Index out of bounds";
        case ED_ERR_INVALID_RANGE:
            return "Invalid range";
        case ED_ERR_INVALID_INPUT:
            return "Invalid input";

        /* Operation errors */
        case ED_ERR_NOT_FOUND:
            return "Not found";
        case ED_ERR_NOT_SUPPORTED:
            return "Operation not supported";
        case ED_ERR_NO_MATCH:
            return "No match found";
        case ED_ERR_CANCELLED:
            return "Operation cancelled";

        /* System errors */
        case ED_ERR_SYSTEM:
            return "System error";
        case ED_ERR_UNKNOWN:
        default:
            return "Unknown error";
    }
}
