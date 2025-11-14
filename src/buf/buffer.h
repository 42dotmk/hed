#ifndef BUFFER_H
#define BUFFER_H

/*
 * BUFFER OWNERSHIP AND LIFECYCLE DOCUMENTATION
 * ============================================
 *
 * Memory Ownership Rules:
 * ----------------------
 * 1. Buffer Storage:
 *    - All buffers live in the growable vector E.buffers (BufferVec)
 *    - Buffers are stored in a dynamically-sized array that grows as needed
 *    - No fixed limit on number of buffers (previously MAX_BUFFERS = 256)
 *    - When a buffer is "closed", its resources are freed and remaining buffers shift down
 *
 * 2. String Ownership (filename, title, filetype):
 *    - These strings are OWNED by the Buffer struct
 *    - They are allocated with strdup() when the buffer is created/modified
 *    - They MUST be freed when the buffer is closed (via buf_close)
 *    - Callers should NOT free these strings; the buffer owns them
 *
 * 3. Row Array Ownership (rows):
 *    - The rows array is OWNED by the Buffer struct
 *    - Allocated with realloc() as rows are added/removed
 *    - Each Row within contains its own owned SizedStr data
 *    - All rows and their data are freed when the buffer is closed
 *
 * 4. Tree-sitter State (ts_internal):
 *    - Opaque pointer OWNED by the Buffer struct
 *    - Managed by tree-sitter subsystem (see src/utils/ts.c)
 *    - Freed via ts_buffer_free() when buffer is closed
 *
 * Buffer Lifecycle:
 * ----------------
 * 1. Creation: buf_new() or buf_open_file()
 *    - Allocates a slot in E.buffers[]
 *    - Allocates owned strings (filename, title, filetype)
 *    - Initializes cursor positions and state
 *
 * 2. Active Use: Buffer is referenced by index
 *    - Windows reference buffers via buffer_index (NOT pointers)
 *    - Multiple windows can reference the same buffer
 *    - E.current_buffer holds the index of the active buffer
 *
 * 3. Closing: buf_close()
 *    - Frees all owned resources (strings, rows, tree-sitter state)
 *    - Shifts remaining buffers down in the array
 *    - Updates all buffer indices in windows to reflect the shift
 *    - IMPORTANT: After closing, all buffer indices > closed_index are decremented
 *
 * Thread Safety:
 * -------------
 * - The buffer system is NOT thread-safe
 * - All buffer operations must occur on the main thread
 * - No concurrent access is supported
 *
 * Common Pitfalls:
 * ---------------
 * - DON'T: Free buffer strings directly (they're owned by the buffer)
 * - DON'T: Hold Buffer* pointers across operations that might close buffers
 * - DON'T: Use buffer pointers after buf_close() (use indices instead)
 * - DO: Use buf_cur() to get the current buffer (checks validity)
 * - DO: Use buffer indices instead of pointers for persistence
 */

#define CURSOR_STYLE_NONE "\x1b[0 q"
#define CURSOR_STYLE_BLOCK "\x1b[1 q"
#define CURSOR_STYLE_UNDERLINE "\x1b[3 q"
#define CURSOR_STYLE_BEAM "\x1b[5 q"
#include "row.h"

/* Buffer structure - represents a single file/document */
typedef struct {
    Row *rows;

    int num_rows;
    int cursor_x;
    int cursor_y;
    char *filename;
    char *title;      /* Display title: filename or "[No Name]" */
    char *filetype;
    int dirty;
    int readonly;     /* Read-only flag (default: 0/false) */
    int visual_start_x;
    int visual_start_y;
    char *cursor_style;
    void *ts_internal; /* tree-sitter per-buffer state (opaque) */
} Buffer;

/* Buffer management */
Buffer *buf_cur(void);

/* Buffer creation and management - all return EdError */
EdError buf_new(const char *filename, int *out_idx);
EdError buf_new_messages(int *out_idx);
EdError buf_close(int index);
EdError buf_switch(int index);
EdError buf_open_file(const char *filename, Buffer **out);

int buf_find_by_filename(const char *filename);  /* Find buffer index by filename, returns -1 if not found */
void buf_next(void);
void buf_prev(void);
void buf_open_or_switch(const char *filename);  /* Open file or switch to it if already open */

void buf_insert_char_in(Buffer *buf, int c);
void buf_insert_newline_in(Buffer *buf);
void buf_del_char_in(Buffer *buf);
void buf_delete_line_in(Buffer *buf);
void buf_yank_line_in(Buffer *buf);
void buf_paste_in(Buffer *buf);
void buf_find_in(Buffer *buf);
/* Filetype detection */
char *buf_detect_filetype(const char *filename);
/* Reload this buffer's file content from disk (discard changes) */
void buf_reload(Buffer *buf);
/* Append message to *messages buffer */
void buf_append_message(const char *msg);

/* New explicit-target variants (prefer these going forward) */
void buf_row_insert_in(Buffer *buf, int at, const char *s, size_t len);
void buf_row_del_in(Buffer *buf, int at);
void buf_row_insert_char_in(Buffer *buf, Row *row, int at, int c);
void buf_row_append_in(Buffer *buf, Row *row, const SizedStr *str);
void buf_row_del_char_in(Buffer *buf, Row *row, int at);

#endif
