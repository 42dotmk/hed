#ifndef BOTTOM_UI_H
#define BOTTOM_UI_H

#include "abuf.h"

typedef struct Layout {
    int term_rows;
    int term_cols;

    int content_rows;   /* rows available for windows */

    int status_row;     /* 1-based terminal row for status bar */

    int qf_rows;        /* total quickfix rows when open, else 0 */
    int qf_header_row;  /* 1-based header row if qf open */

    int cmd_row;        /* 1-based row for command line or first message line */

    int msg_lines;      /* how many message lines are visible */
} Layout;

/* Compute rows/cols distribution for current frame. */
void layout_compute(Layout *lo);

/* Draw UI elements placed below content area */
void draw_status_bar(Abuf *ab, const Layout *lo);
void draw_quickfix(Abuf *ab, const Layout *lo);
void draw_message_bar(Abuf *ab, const Layout *lo);

/* Helper: how many message lines needed for current status text */
int ui_message_lines_needed(void);

#endif /* BOTTOM_UI_H */

