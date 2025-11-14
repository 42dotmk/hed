#ifndef TERM_PANE_H
#define TERM_PANE_H

#include "abuf.h"
#include "window.h"

typedef struct TermPane TermPane;

TermPane *term_pane_get(void);

int term_pane_open(const char *cmd, int height);

void term_pane_poll(void);

void term_pane_close(void);

void term_pane_resize(int rows, int cols);

int term_pane_fd(void);

void term_pane_draw(const Window *win, Abuf *ab);

int term_pane_handle_key(int key);

#endif /* TERM_PANE_H */

