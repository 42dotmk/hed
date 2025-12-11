#include "bottom_ui.h"
#include "ansi.h"
#include "hed.h"

int ui_message_lines_needed(void) {
    if (E.mode == MODE_COMMAND)
        return 1;
    const char *s = E.status_msg;
    int cols = E.screen_cols > 0 ? E.screen_cols : 80;
    int lines = 1, cur = 0;
    for (const char *p = s; *p; ++p) {
        if (*p == '\n') {
            lines++;
            cur = 0;
        } else {
            cur++;
            if (cur >= cols) {
                lines++;
                cur = 0;
            }
        }
    }
    if (lines < 1)
        lines = 1;
    return lines;
}

void layout_compute(Layout *lo) {
    int term_rows = E.screen_rows + 2;
    int term_cols = E.screen_cols;
    (void)get_window_size(&term_rows, &term_cols);
    lo->term_rows = term_rows;
    lo->term_cols = term_cols;

    int base_rows = term_rows > 2 ? (term_rows - 2) : term_rows;
    int needed = ui_message_lines_needed();
    int avail_rows = base_rows - (needed - 1);
    int qf_rows = (E.qf.open && E.qf.height > 0) ? E.qf.height : 0;
    if (avail_rows < 1)
        avail_rows = 1;
    lo->content_rows = avail_rows; /* includes any panes like quickfix when
                                      integrated in layout */
    lo->status_row = lo->content_rows + 1;
    lo->qf_rows = qf_rows; /* informational only now */
    lo->qf_header_row = 0; /* drawing handled via layout */
    lo->msg_lines = (E.mode == MODE_COMMAND) ? 1 : needed;
    /* Command/message row sits directly below status bar; quickfix is part of
     * content now */
    lo->cmd_row = lo->status_row + 1;
}

void draw_status_bar(Abuf *ab, const Layout *lo) {
    Buffer *buf = buf_cur();
    char status[80], rstatus[80];
    int len =
        snprintf(status, sizeof(status), " %s%s", buf ? buf->title : "[NoBuf]",
                 buf && buf->dirty ? "*" : "");
    Window *cwin = window_cur();
    int rlen =
        snprintf(rstatus, sizeof(rstatus), "%d:%d ",
                 cwin ? cwin->cursor.y + 1 : 1, cwin ? cwin->cursor.x + 1 : 1);
    if (len > lo->term_cols)
        len = lo->term_cols;
    ansi_move(ab, lo->status_row, 1);
    ansi_clear_eol(ab);
    ab_append(ab, status, len);
    while (len < lo->term_cols) {
        if (lo->term_cols - len == rlen) {
            ab_append(ab, rstatus, rlen);
            break;
        }
        ab_append(ab, " ", 1);
        len++;
    }
}

void draw_message_bar(Abuf *ab, const Layout *lo) {
    if (E.mode == MODE_COMMAND) {
        ansi_move(ab, lo->cmd_row, 1);
        ansi_clear_eol(ab);
        ab_append(ab, ":", 1);
        int msglen = E.command_len;
        if (msglen > lo->term_cols - 1)
            msglen = lo->term_cols - 1;
        if (msglen > 0)
            ab_append(ab, E.command_buf, msglen);
        return;
    }
    const char *s = E.status_msg;
    int cols = lo->term_cols;
    int drawn = 0;
    const char *p = s;
    while (drawn < lo->msg_lines) {
        ansi_move(ab, lo->cmd_row + drawn, 1);
        ansi_clear_eol(ab);
        int used = 0;
        while (*p && *p != '\n' && used < cols) {
            ab_append(ab, p, 1);
            p++;
            used++;
        }
        if (*p == '\n')
            p++;
        drawn++;
    }
}
