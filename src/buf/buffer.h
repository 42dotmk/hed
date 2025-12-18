#ifndef BUFFER_H
#define BUFFER_H

#define CURSOR_STYLE_NONE "\x1b[0 q"
#define CURSOR_STYLE_BLOCK "\x1b[1 q"
#define CURSOR_STYLE_UNDERLINE "\x1b[3 q"
#define CURSOR_STYLE_BEAM "\x1b[5 q"

#include "cursor.h"
#include "errors.h"
#include "row.h"
#include "utils/fold.h"

/* FoldMethod enum for automatic fold detection */
typedef enum {
    FOLD_METHOD_MANUAL,   /* No automatic folding, manual only */
    FOLD_METHOD_BRACKET,  /* Bracket-based folding { } */
    FOLD_METHOD_INDENT,   /* Indentation-based folding */
} FoldMethod;

/* Buffer structure - represents a single file/document */
typedef struct Buffer {
    Row *rows;
    int num_rows;
    Cursor cursor;
    char *cursor_style;

    char *filename;
    char *title; /* Display title: filename or "[No Name]" */
    char *filetype;
    int dirty;
    int readonly; /* Read-only flag (default: 0/false) */
    void *ts_internal; /* tree-sitter per-buffer state (opaque) */

    FoldList folds; /* Code folding regions */
    FoldMethod fold_method; /* Active fold detection method */
} Buffer;

/* Buffer management */
Buffer *buf_cur(void);

/* Buffer creation and management - all return EdError */
EdError buf_new(const char *filename, int *out_idx);
EdError buf_close(int index);
EdError buf_switch(int index);
EdError buf_open_file(const char *filename, Buffer **out);

int buf_find_by_filename(
    const char
        *filename); /* Find buffer index by filename, returns -1 if not found */
void buf_next(void);
void buf_prev(void);
void buf_open_or_switch(const char *filename, bool add_to_jumplist); /* Open file or switch to it if already open */
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
#endif
