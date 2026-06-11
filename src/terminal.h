#ifndef TERMINAL_H
#define TERMINAL_H

#include "lib/errors.h"
#include "buf/buffer.h"
#include "ui/window.h"
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

/* Mouse reporting (xterm button-event tracking 1002 + SGR 1006).
 * term_mouse_set writes the escapes immediately and remembers the
 * desired state so enable_raw_mode re-arms it after shell-outs
 * (term_cmd, :fmt, :reload all bounce raw mode). disable_raw_mode
 * always turns reporting off so spawned programs and the shell get a
 * normal terminal back. */
void term_mouse_set(int on);
int  term_mouse_get(void);

EdError buf_save_in(Buffer *buf);

/* Output / Rendering */
/* Scroll the active window to keep cursor visible */
void window_scroll(Window *win);
/* Render a full editor frame (all windows + UI) */
void ed_render_frame(void);

/* Map a terminal cell (1-based srow/scol, e.g. from a mouse event)
 * inside `win` to a buffer position. Inverse of the renderer's
 * visual-row walk: accounts for the line-number gutter, folds,
 * horizontal scroll, soft wrap sublines and block-below virtual text
 * rows (clicks on virtual rows snap to their anchor line). Cells past
 * the end of a line clamp to the last character; cells below EOF clamp
 * to the last visible line. Returns 1 and fills out_y (row index) /
 * out_x (char index) on success, 0 when the window has no buffer or
 * the cell is outside its content area. */
int window_screen_to_buffer(const Window *win, int srow, int scol,
                            int *out_y, int *out_x);

#endif
