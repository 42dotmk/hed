#ifndef ANSI_H
#define ANSI_H

#include "abuf.h"

static inline void ansi_hide_cursor(Abuf *ab) { ab_append(ab, "\x1b[?25l", 6); }
static inline void ansi_show_cursor(Abuf *ab) { ab_append(ab, "\x1b[?25h", 6); }
static inline void ansi_clear_eol(Abuf *ab) { ab_append(ab, "\x1b[K", 3); }
static inline void ansi_home(Abuf *ab) { ab_append(ab, "\x1b[H", 3); }
static inline void ansi_move(Abuf *ab, int row, int col) {
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", row, col);
    if (n > 0) ab_append(ab, buf, n);
}
static inline void ansi_invert_on(Abuf *ab) { ab_append(ab, "\x1b[7m", 4); }
static inline void ansi_sgr_reset(Abuf *ab) { ab_append(ab, "\x1b[m", 3); }
static inline void ansi_set_fg_color(Abuf *ab, int color) {
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "\x1b[38;5;%dm", color);
    if (n > 0) ab_append(ab, buf, n);
}
static inline void ansi_set_bg_color(Abuf *ab, int color) {
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "\x1b[48;5;%dm", color);
    if (n > 0) ab_append(ab, buf, n);
}
static inline void ansi_clear_screen(Abuf *ab) { ab_append(ab, "\x1b[2J", 4); }
#endif /* ANSI_H */
