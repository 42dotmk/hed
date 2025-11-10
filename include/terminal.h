#ifndef TERMINAL_H
#define TERMINAL_H

#include <termios.h>

/* Global terminal state */
extern struct termios orig_termios;

/* Terminal functions */
void die(const char *s);
/* Disable raw mode for terminal input, must be called on exit */
void disable_raw_mode(void);
/* Enable raw mode for terminal input */
void enable_raw_mode(void);
int get_cursor_position(int *rows, int *cols);
int get_window_size(int *rows, int *cols);

/* File I/O */
char *buf_rows_to_string(int *buflen);
void buf_open(char *filename);
void buf_save(void);

/* Output / Rendering */
/* Scroll the active window to keep cursor visible */
void window_scroll(Window *win);
/* Redraw the entire screen */
void buf_refresh_screen(void);

#endif
