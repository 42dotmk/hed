#ifndef WINDOW_H
#define WINDOW_H

/* A view onto a buffer with its own geometry.
 * For now we support a single window, but the struct allows future splits.
 */
#include "buffer.h"

typedef struct {
    int width;/* in characters or rows */
    int height; /* in characters or rows */
    Row *rows; /* array of strings for the content */
} WinDecor;

typedef struct {
    int enabled; 
    WinDecor left;
    WinDecor Right;
    WinDecor Top;
    WinDecor Bottom;
} WindowDecorations;

typedef struct {
    int top;            /* 1-based row on the terminal */
    int left;           /* 1-based column on the terminal */
    int height;         /* number of content rows */
    int width;          /* number of columns */
    int buffer_index;   /* index into E.buffers */
    int focus;          /* 1 if focused */
    int row_offset;     /* first visible buffer row */
    int col_offset;     /* first visible buffer column (render x) */
    int cursor_x;       /* window-local cursor column (in chars) */
    int cursor_y;       /* window-local cursor row (line index) */
    int visual_start_x; /* visual selection anchor (char index) */
    int visual_start_y; /* visual selection anchor row */
    WindowDecorations decorations;
} Window;


/* Window management */
void windows_init(void);
void windows_set_geometry(int top, int left, int height, int width);
Window *window_cur(void);

/* Simple 2-way splits */
void windows_split_vertical(void);   /* side-by-side */
void windows_split_horizontal(void); /* stacked */
void windows_focus_next(void);       /* cycle focus */
void windows_on_resize(int content_rows, int cols);
void windows_close_current(void);    /* close focused window */

#endif /* WINDOW_H */
