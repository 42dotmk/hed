#ifndef WINDOW_H
#define WINDOW_H

#include "buffer.h"

/* Unified selection description for yanks/deletes */
typedef enum {
    SEL_NONE = 0,        /* No selection */
    SEL_VISUAL,          /* Character-wise (visual mode) */
    SEL_VISUAL_LINE,     /* Line-wise (yy, dd, etc.) */
    SEL_VISUAL_BLOCK     /* Block/column-wise (visual block mode) */
} SelectionType;

typedef struct {
    SelectionType type;
    int anchor_y, anchor_x;
    int cursor_y, cursor_x;
    int anchor_rx; /* render column for block anchor */
    int block_start_rx,
        block_end_rx; /* for SEL_BLOCK: render columns, end exclusive */
} Selection;

typedef struct Window {
    int top;          /* 1-based row on the terminal */
    int left;         /* 1-based column on the terminal */
    int height;       /* number of content rows */
    int width;        /* number of columns */
    int buffer_index; /* index into E.buffers */

    int focus;        /* 1 if focused */
    int is_quickfix;  /* 1 if this window is a quickfix pane */
    int is_modal;     /* 1 if this is a modal window */
    int visible;      /* 1 if visible (for showing/hiding modals) */
    int wrap;         /* 1 if soft-wrap is enabled */

    int row_offset;   /* first visible visual row (wrap-aware) */
    int col_offset;   /* first visible buffer column (render x) */
    /* Gutter configuration: 0=off, 1=auto line numbers, 2=fixed width */
    int gutter_mode;
    int gutter_fixed_width;
    /* Visual selection state */
    Selection sel;
} Window;

/* Window management */
void windows_init(void);
Window *window_cur(void);
void win_attach_buf(Window *win, Buffer *buf);

/* Simple 2-way splits */
void windows_split_vertical(void);   /* side-by-side */
void windows_split_horizontal(void); /* stacked */
void windows_focus_next(void);       /* cycle focus */
void windows_close_current(void);    /* close focused window */

/* Directional focus helpers */
void windows_focus_left(void);
void windows_focus_right(void);
void windows_focus_up(void);
void windows_focus_down(void);

#endif /* WINDOW_H */
