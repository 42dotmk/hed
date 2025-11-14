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

EdError buf_save_in(Buffer *buf);

/* Output / Rendering */
/* Scroll the active window to keep cursor visible */
void window_scroll(Window *win);
/* Render a full editor frame (all windows + UI) */
void ed_render_frame(void);

#endif
