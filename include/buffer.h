#ifndef BUFFER_H
#define BUFFER_H

#include "sizedstr.h"

#define CURSOR_STYLE_NONE "\x1b[0 q"
#define CURSOR_STYLE_BLOCK "\x1b[1 q"
#define CURSOR_STYLE_UNDERLINE "\x1b[3 q"
#define CURSOR_STYLE_BEAM "\x1b[5 q"

/* Text row structure */
typedef struct {
    SizedStr chars;   /* Original text */
    SizedStr render;  /* Rendered text (with tabs expanded) */
} Row;

/* Buffer structure - represents a single file/document */
typedef struct {
    Row *rows;

    int num_rows;
    int cursor_x;
    int cursor_y;
    char *filename;
    char *filetype;
    int dirty;
    int visual_start_x;
    int visual_start_y;
    char *cursor_style;
    void *ts_internal; /* tree-sitter per-buffer state (opaque) */
} Buffer;

/* Buffer management */
Buffer *buf_cur(void);
int buf_new(char *filename);
void buf_switch(int index);
void buf_next(void);
void buf_prev(void);
void buf_close(int index);
void buf_list(void);

void buf_insert_char(int c);
void buf_insert_newline(void);
void buf_del_char(void);
void buf_delete_line(void);
void buf_yank_line(void);
void buf_paste(void);
void buf_find(void);
/* Filetype detection */
char *buf_detect_filetype(const char *filename);
/* Reload current buffer's file content from disk (discard changes) */
void buf_reload_current(void);

/* Row operations */
int buf_row_cx_to_rx(const Row *row, int cx);
int buf_row_rx_to_cx(const Row *row, int rx);
void buf_row_update(Row *row);
void buf_row_insert(int at, const char *s, size_t len);
void buf_row_free(Row *row);
void buf_row_del(int at);
void buf_row_insert_char(Row *row, int at, int c);
void buf_row_append(Row *row, const SizedStr *str);
void buf_row_del_char(Row *row, int at);

#endif
