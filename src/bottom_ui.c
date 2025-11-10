#include "hed.h"
#include "bottom_ui.h"
#include "ansi.h"

int ui_message_lines_needed(void) {
    if (E.mode == MODE_COMMAND) return 1;
    const char *s = E.status_msg;
    int cols = E.screen_cols > 0 ? E.screen_cols : 80;
    int lines = 1, cur = 0;
    for (const char *p = s; *p; ++p) {
        if (*p == '\n') { lines++; cur = 0; }
        else { cur++; if (cur >= cols) { lines++; cur = 0; } }
    }
    if (lines < 1) lines = 1;
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
    if (avail_rows - qf_rows < 1) avail_rows = qf_rows + 1;
    lo->content_rows = avail_rows - qf_rows;
    lo->status_row = lo->content_rows + 1;
    lo->qf_rows = qf_rows;
    lo->qf_header_row = qf_rows ? (lo->status_row + 1) : 0;
    lo->msg_lines = (E.mode == MODE_COMMAND) ? 1 : needed;
    lo->cmd_row = lo->status_row + 1 + qf_rows;
}

void draw_status_bar(Abuf *ab, const Layout *lo) {
    Buffer *buf = buf_cur();
    char status[80], rstatus[80];
    const char *mode_str = (E.mode==MODE_NORMAL?"NORMAL":E.mode==MODE_INSERT?"INSERT":E.mode==MODE_VISUAL?"VISUAL":"COMMAND");
    int len = snprintf(status, sizeof(status), " [%d/%d] %.20s - %d lines %s%s",
        E.current_buffer + 1, E.num_buffers,
        buf && buf->filename ? buf->filename : "[No Name]",
        buf ? buf->num_rows : 0,
        buf && buf->dirty ? "*" : "", mode_str);
    Window *cwin = window_cur();
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d:%d ",
        cwin ? cwin->cursor_y + 1 : 1, cwin ? cwin->cursor_x + 1 : 1);
    if (len > lo->term_cols) len = lo->term_cols;
    ansi_move(ab, lo->status_row, 1);
    ansi_clear_eol(ab);
    ab_append(ab, status, len);
    while (len < lo->term_cols) {
        if (lo->term_cols - len == rlen) { ab_append(ab, rstatus, rlen); break; }
        ab_append(ab, " ", 1); len++;
    }
}

void draw_message_bar(Abuf *ab, const Layout *lo) {
    if (E.mode == MODE_COMMAND) {
        ansi_move(ab, lo->cmd_row, 1);
        ansi_clear_eol(ab);
        ab_append(ab, ":", 1);
        int msglen = E.command_len;
        if (msglen > lo->term_cols - 1) msglen = lo->term_cols - 1;
        if (msglen > 0) ab_append(ab, E.command_buf, msglen);
        return;
    }
    const char *s = E.status_msg; int cols = lo->term_cols; int drawn = 0; const char *p = s;
    while (drawn < lo->msg_lines) {
        ansi_move(ab, lo->cmd_row + drawn, 1);
        ansi_clear_eol(ab);
        int used = 0;
        while (*p && *p != '\n' && used < cols) { ab_append(ab, p, 1); p++; used++; }
        if (*p == '\n') p++;
        drawn++;
    }
}

void draw_quickfix(Abuf *ab, const Layout *lo) {
    if (!E.qf.open || E.qf.height <= 0) return;
    int width = lo->term_cols;
    /* Header */
    char hdr[128];
    int hlen = snprintf(hdr, sizeof(hdr), " Quickfix (%d items)  j/k navigate  Enter open  q close ", E.qf.len);
    if (hlen > width) hlen = width;
    ansi_move(ab, lo->qf_header_row, 1);
    ab_append(ab, hdr, hlen);
    ansi_sgr_reset(ab);
    ansi_clear_eol(ab);
    /* Items */
    int lines = E.qf.height - 1; if (lines < 0) lines = 0;
    int start = E.qf.scroll;
    for (int row = 0; row < lines; row++) {
        int idx = start + row;
        ansi_move(ab, lo->qf_header_row + 1 + row, 1);
        if (idx >= E.qf.len) { ansi_clear_eol(ab); continue; }
        const QfItem *it = &E.qf.items[idx];
        char line[512]; int l = 0;
        if (it->filename && it->filename[0]) l = snprintf(line, sizeof(line), "%s:%d:%d: %s", it->filename, it->line, it->col, it->text ? it->text : "");
        else l = snprintf(line, sizeof(line), "%d:%d: %s", it->line, it->col, it->text ? it->text : "");
        if (l > width) l = width;
        if (l > 0) ab_append(ab, line, l);
        if (idx == E.qf.sel) ansi_sgr_reset(ab);
        ansi_clear_eol(ab);
    }
}
