#include "utils/bottom_ui.h"
#include "lib/ansi.h"
#include "editor.h"
#include "prompt.h"
#include "terminal.h"

/* Count rows needed by `s`, wrapping at the terminal width. */
static int count_msg_lines(const char *s) {
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
    if (lines < 1) lines = 1;
    return lines;
}

int ui_message_lines_needed(void) {
    if (prompt_active()) {
        Prompt *pr = prompt_current();
        if (!pr) return 1;
        int prompt_lines = 1;
        for (int i = 0; i < pr->len; i++)
            if (pr->buf[i] == '\n') prompt_lines++;
        /* Stack the prompt's own hint above the input when one is set
         * (e.g. Tab-completion's "N matches" message). E.status_msg is
         * skipped here — async plugin chatter would clobber the hint
         * before the user can read it. */
        int hint_lines = (pr->hint[0]) ? count_msg_lines(pr->hint) : 0;
        return prompt_lines + hint_lines;
    }
    return count_msg_lines(E.status_msg);
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
    lo->msg_lines = needed;
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

/* Render `s` into `ab` starting at `start_row`, wrapping at term width.
 * Returns the number of rows consumed. */
static int draw_wrapped_text(Abuf *ab, const Layout *lo, int start_row,
                             const char *s) {
    int cols = lo->term_cols;
    int row = start_row;
    int used = 0;
    ansi_move(ab, row, 1);
    ansi_clear_eol(ab);
    for (const char *p = s; *p; p++) {
        if (*p == '\n') {
            row++;
            used = 0;
            ansi_move(ab, row, 1);
            ansi_clear_eol(ab);
            continue;
        }
        if (used >= cols) {
            row++;
            used = 0;
            ansi_move(ab, row, 1);
            ansi_clear_eol(ab);
        }
        ab_append(ab, p, 1);
        used++;
    }
    return row - start_row + 1;
}

void draw_message_bar(Abuf *ab, const Layout *lo) {
    Prompt *pr = prompt_current();
    if (pr) {
        /* The prompt's own hint (if any) is rendered above the input.
         * Lives in the prompt — async E.status_msg writes don't clobber
         * it, so completion feedback stays put. */
        int row = lo->cmd_row;
        if (pr->hint[0]) {
            int n = draw_wrapped_text(ab, lo, row, pr->hint);
            row += n;
        }
        const char *label = pr->vt->label ? pr->vt->label(pr) : "";
        int label_len = (int)strlen(label);
        if (label_len > lo->term_cols) label_len = lo->term_cols;
        ansi_move(ab, row, 1);
        ansi_clear_eol(ab);
        ab_append(ab, label, label_len);
        int line_idx = 0;
        int avail = lo->term_cols - label_len;
        for (int i = 0; i < pr->len; i++) {
            char c = pr->buf[i];
            if (c == '\n') {
                line_idx++;
                ansi_move(ab, row + line_idx, 1);
                ansi_clear_eol(ab);
                avail = lo->term_cols;
                continue;
            }
            if (avail > 0) {
                ab_append(ab, &c, 1);
                avail--;
            }
        }
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
