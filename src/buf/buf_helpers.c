#include "hed.h"

/* Internal low-level row helpers (not part of public API) */
void buf_row_insert_in(Buffer *buf, int at, const char *s, size_t len);
void buf_row_del_in(Buffer *buf, int at);
void buf_row_append_in(Buffer *buf, Row *row, const SizedStr *str);

/*** Cursor movement helpers ***/

static inline void cur_sync_from_window(Buffer *buf, Window *win) {
    if (PTR_VALID(buf) && PTR_VALID(win)) { 
         buf->cursor.x = win->cursor.x; 
         buf->cursor.y = win->cursor.y; 
    }
}
static inline void cur_sync_to_window(Buffer *buf, Window *win) {
    if (PTR_VALID(buf) && PTR_VALID(win)) { win->cursor.x = buf->cursor.x; win->cursor.y = buf->cursor.y; }
}

/*
 * Macro to eliminate cursor sync boilerplate in helper functions.
 * Usage: CURSOR_OP({ code that modifies buf->cursor_x/y })
 */
#define CURSOR_OP(code) do { \
    Buffer *buf = buf_cur(); \
    Window *win = window_cur(); \
    if (!PTR_VALID(buf) || !PTR_VALID(win)) return; \
    cur_sync_from_window(buf, win); \
    code \
    cur_sync_to_window(buf, win); \
} while(0)

/* Visual-line helpers for soft-wrap navigation */
static int row_visual_height_buf(const Buffer *buf, int row_index, int content_cols, int wrap) {
    if (!wrap) return 1;
    if (!buf) return 1;
    if (row_index < 0 || row_index >= buf->num_rows) return 1;
    if (content_cols <= 0) return 1;
    const Row *row = &buf->rows[row_index];
    int rcols = buf_row_cx_to_rx(row, (int)row->chars.len);
    if (rcols <= 0) return 1;
    int h = (rcols + content_cols - 1) / content_cols;
    return h < 1 ? 1 : h;
}

static int cursor_visual_position(const Buffer *buf, const Window *win,
                                  int content_cols, int *out_vis_col) {
    if (!buf || !win || buf->num_rows <= 0) {
        if (out_vis_col) *out_vis_col = 0;
        return 0;
    }
    int cy = win->cursor.y;
    if (cy < 0) cy = 0;
    if (cy >= buf->num_rows) cy = buf->num_rows - 1;

    int visual = 0;
    for (int y = 0; y < cy; y++) {
        visual += row_visual_height_buf(buf, y, content_cols, 1);
    }
    const Row *row = &buf->rows[cy];
    int rx = buf_row_cx_to_rx(row, win->cursor.x);
    if (rx < 0) rx = 0;
    int h = row_visual_height_buf(buf, cy, content_cols, 1);
    int seg = rx / content_cols;
    if (seg >= h) seg = h - 1;
    int vis_col = rx % content_cols;
    if (out_vis_col) *out_vis_col = vis_col;
    return visual + seg;
}

static int buffer_total_visual_rows(const Buffer *buf, int content_cols) {
    if (!buf || buf->num_rows <= 0) return 0;
    int total = 0;
    for (int y = 0; y < buf->num_rows; y++) {
        total += row_visual_height_buf(buf, y, content_cols, 1);
    }
    return total;
}

static void cursor_from_visual(Buffer *buf, Window *win,
                               int target_visual, int content_cols, int vis_col) {
    if (!buf || !win || buf->num_rows <= 0) {
        win->cursor.y = 0;
        win->cursor.x = 0;
        return;
    }
    if (target_visual < 0) target_visual = 0;

    int y = 0;
    while (y < buf->num_rows) {
        int h = row_visual_height_buf(buf, y, content_cols, 1);
        if (target_visual < h) break;
        target_visual -= h;
        y++;
    }
    if (y >= buf->num_rows) {
        y = buf->num_rows - 1;
        if (y < 0) {
            win->cursor.y = 0;
            win->cursor.x = 0;
            return;
        }
        int h = row_visual_height_buf(buf, y, content_cols, 1);
        target_visual = h - 1;
        if (target_visual < 0) target_visual = 0;
    }

    Row *row = &buf->rows[y];
    int rcols = buf_row_cx_to_rx(row, (int)row->chars.len);
    if (rcols < 0) rcols = 0;
    if (content_cols <= 0) content_cols = 1;

    int seg_start = target_visual * content_cols;
    if (seg_start > rcols) {
        seg_start = (rcols / content_cols) * content_cols;
    }
    int rx = seg_start + vis_col;
    int seg_end = seg_start + content_cols;
    if (rx >= seg_end) rx = seg_end - 1;
    if (rx >= rcols && rcols > 0) rx = rcols - 1;
    if (rcols == 0) rx = 0;

    int cx = buf_row_rx_to_cx(row, rx);
    win->cursor.y = y;
    win->cursor.x = cx;
}

void buf_cursor_move_top(void) {
    CURSOR_OP({
        buf->cursor.y = 0;
        buf->cursor.x = 0;
    });
}

void buf_cursor_move_bottom(void) {
    CURSOR_OP({
        buf->cursor.y = buf->num_rows - 1;
        if (buf->cursor.y < 0) buf->cursor.y = 0;
        buf->cursor.x = 0;
    });
}

void buf_cursor_move_line_start(void) {
    CURSOR_OP({
        buf->cursor.x = 0;
    });
}

void buf_cursor_move_line_end(void) {
    CURSOR_OP({
        if (BOUNDS_CHECK(buf->cursor.y, buf->num_rows)) {
            buf->cursor.x = buf->rows[buf->cursor.y].chars.len;
        }
    });
}

void buf_cursor_move_word_forward(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win)) return;
    cur_sync_from_window(buf, win);
    if (!BOUNDS_CHECK(buf->cursor.y, buf->num_rows)) { cur_sync_to_window(buf, win); return; }

    Row *row = &buf->rows[buf->cursor.y];

    /* Skip current word */
    while (buf->cursor.x < (int)row->chars.len &&
           !isspace(row->chars.data[buf->cursor.x])) {
        buf->cursor.x++;
    }

    /* Skip whitespace */
    while (buf->cursor.x < (int)row->chars.len &&
           isspace(row->chars.data[buf->cursor.x])) {
        buf->cursor.x++;
    }

    /* If at end of line, move to next line */
    if (buf->cursor.x >= (int)row->chars.len &&
        buf->cursor.y < buf->num_rows - 1) {
        buf->cursor.y++;
        buf->cursor.x = 0;
    }
    cur_sync_to_window(buf, win);
}

void buf_cursor_move_word_backward(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win)) return;
    cur_sync_from_window(buf, win);

    /* If at start of line, go to end of previous line */
    if (buf->cursor.x == 0) {
        if (buf->cursor.y > 0) {
            buf->cursor.y--;
            if (BOUNDS_CHECK(buf->cursor.y, buf->num_rows)) {
                buf->cursor.x = buf->rows[buf->cursor.y].chars.len;
            }
        }
        cur_sync_to_window(buf, win);
        return;
    }

    Row *row = &buf->rows[buf->cursor.y];
    buf->cursor.x--;

    /* Skip whitespace */
    while (buf->cursor.x > 0 && isspace(row->chars.data[buf->cursor.x])) {
        buf->cursor.x--;
    }

    /* Skip word */
    while (buf->cursor.x > 0 && !isspace(row->chars.data[buf->cursor.x - 1])) {
        buf->cursor.x--;
    }
    cur_sync_to_window(buf, win);
}

/*** Screen positioning helpers ***/

void buf_center_screen(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(win)) return;
    /* Center current line in the middle of the current window (logical rows) */
    if (!buf || win->wrap) return;
    win->row_offset = win->cursor.y - (win->height / 2);
    if (win->row_offset < 0) win->row_offset = 0;
    int maxoff = buf->num_rows - win->height;
    if (win->row_offset > maxoff) {
        win->row_offset = maxoff;
        if (win->row_offset < 0) win->row_offset = 0;
    }
}

void buf_scroll_half_page_up(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win)) return;
    int half_page = E.screen_rows / 2;
    win->cursor.y -= half_page;
    if (win->cursor.y < 0) win->cursor.y = 0;
}

void buf_scroll_half_page_down(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win)) return;
    int half_page = E.screen_rows / 2;
    win->cursor.y += half_page;
    if (win->cursor.y >= buf->num_rows) {
        win->cursor.y = buf->num_rows - 1;
        if (win->cursor.y < 0) win->cursor.y = 0;
    }
}

void buf_scroll_page_up(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win)) return;
    win->cursor.y -= E.screen_rows;
    if (win->cursor.y < 0) win->cursor.y = 0;
}

void buf_scroll_page_down(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win)) return;
    win->cursor.y += E.screen_rows;
    if (win->cursor.y >= buf->num_rows) {
        win->cursor.y = buf->num_rows - 1;
        if (win->cursor.y < 0) win->cursor.y = 0;
    }
}

/*** Line operations helpers ***/

void buf_join_lines(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win) || win->cursor.y >= buf->num_rows - 1) return;

    int y = win->cursor.y;
    Row *current = &buf->rows[y];
    Row *next = &buf->rows[y + 1];
    if (!PTR_VALID(current) || !PTR_VALID(next)) return;

    int cx_before = win->cursor.x;
    int need_space = (current->chars.len > 0 &&
                      current->chars.data[current->chars.len - 1] != ' ');

    int started_group = 0;
    if (!undo_is_applying()) {
        undo_begin_group();
        started_group = 1;
    }

    /* Optional space insertion at end of current line */
    if (need_space) {
        char space = ' ';
        int cx_ins = (int)current->chars.len;
        if (!undo_is_applying()) {
            undo_push_insert(y, cx_ins, &space, 1,
                             y, cx_before, y, cx_before);
        }
        sstr_append_char(&current->chars, ' ');
        buf_row_update(current);
    }

    /* Delete logical newline between current and next line (merge lines) */
    int cx_nl = (int)current->chars.len;
    if (!undo_is_applying()) {
        const char nl = '\n';
        undo_push_delete(y, cx_nl, &nl, 1,
                         y, cx_before, y, cx_before);
    }
    /* Append next line's text and remove the next row */
    buf_row_append_in(buf, current, &next->chars);
    buf_row_del_in(buf, y + 1);
    buf->dirty++;

    if (started_group && !undo_is_applying()) {
        undo_commit_group();
    }
}

void buf_duplicate_line(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win)) return;
    if (!BOUNDS_CHECK(win->cursor.y, buf->num_rows)) return;
    /* Implement as yank + paste so undo works naturally */
    buf_yank_line_in(buf);
    buf_paste_in(buf);
}

void buf_move_line_up(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win) || win->cursor.y == 0 || buf->num_rows < 2) return;

    /* Swap with previous line */
    Row temp = buf->rows[win->cursor.y];
    buf->rows[win->cursor.y] = buf->rows[win->cursor.y - 1];
    buf->rows[win->cursor.y - 1] = temp;
    win->cursor.y--;
}

void buf_move_line_down(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win) || win->cursor.y >= buf->num_rows - 1) return;

    /* Swap with next line */
    Row temp = buf->rows[win->cursor.y];
    buf->rows[win->cursor.y] = buf->rows[win->cursor.y + 1];
    buf->rows[win->cursor.y + 1] = temp;
    win->cursor.y++;
}

/*** Text manipulation helpers ***/

void buf_indent_line(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win) || !BOUNDS_CHECK(win->cursor.y, buf->num_rows)) return;
    if (buf->readonly) {
        ed_set_status_message("Buffer is read-only");
        return;
    }

    Row *row = &buf->rows[win->cursor.y];

    int y = win->cursor.y;
    int cx_before = win->cursor.x;

    if (!undo_is_applying()) {
        char spaces[TAB_STOP];
        for (int i = 0; i < TAB_STOP; i++) spaces[i] = ' ';
        int cx_after = cx_before + TAB_STOP;
        undo_begin_group();
        undo_push_insert(y, 0, spaces, (size_t)TAB_STOP,
                         y, cx_before, y, cx_after);
        undo_commit_group();
    }

    /* Insert TAB_STOP spaces at the beginning */
    for (int i = 0; i < TAB_STOP; i++) {
        sstr_insert_char(&row->chars, 0, ' ');
    }

    buf_row_update(row);
    win->cursor.x += TAB_STOP;
    buf->dirty++;
}

void buf_unindent_line(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win) || !BOUNDS_CHECK(win->cursor.y, buf->num_rows)) return;
    if (buf->readonly) {
        ed_set_status_message("Buffer is read-only");
        return;
    }

    Row *row = &buf->rows[win->cursor.y];

    /* Remove up to TAB_STOP spaces from the beginning */
    int spaces_to_remove = 0;
    for (int i = 0; i < TAB_STOP && i < (int)row->chars.len; i++) {
        if (row->chars.data[i] == ' ') {
            spaces_to_remove++;
        } else {
            break;
        }
    }

    int y = win->cursor.y;
    int cx_before = win->cursor.x;

    if (spaces_to_remove > 0 && !undo_is_applying()) {
        char removed[8];
        for (int i = 0; i < spaces_to_remove && i < (int)sizeof(removed); i++) removed[i] = ' ';
        int cx_after = cx_before - spaces_to_remove;
        if (cx_after < 0) cx_after = 0;
        undo_begin_group();
        undo_push_delete(y, 0, removed, (size_t)spaces_to_remove,
                         y, cx_before, y, cx_after);
        undo_commit_group();
    }

    for (int i = 0; i < spaces_to_remove; i++) {
        sstr_delete_char(&row->chars, 0);
    }

    buf_row_update(row);
    win->cursor.x -= spaces_to_remove;
    if (win->cursor.x < 0) win->cursor.x = 0;
    buf->dirty++;
}

void buf_toggle_comment(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win) || !BOUNDS_CHECK(win->cursor.y, buf->num_rows)) return;
    if (buf->readonly) {
        ed_set_status_message("Buffer is read-only");
        return;
    }

    /* Determine comment string based on filetype */
    const char *comment = "// ";
    if (buf->filetype) {
        if (strcmp(buf->filetype, "python") == 0) comment = "# ";
        else if (strcmp(buf->filetype, "shell") == 0) comment = "# ";
        else if (strcmp(buf->filetype, "c") == 0) comment = "// ";
        else if (strcmp(buf->filetype, "cpp") == 0) comment = "// ";
        else if (strcmp(buf->filetype, "javascript") == 0) comment = "// ";
        else if (strcmp(buf->filetype, "rust") == 0) comment = "// ";
        else if (strcmp(buf->filetype, "go") == 0) comment = "// ";
    }

    int y = win->cursor.y;
    Row *row = &buf->rows[y];
    int comment_len = strlen(comment);

    /* Check if line starts with comment */
    int is_commented = 1;
    for (int i = 0; i < comment_len; i++) {
        if (i >= (int)row->chars.len || row->chars.data[i] != comment[i]) {
            is_commented = 0;
            break;
        }
    }

    if (!undo_is_applying()) {
        undo_begin_group();
    }

    if (is_commented) {
        /* Remove comment */
        if (!undo_is_applying()) {
            undo_push_delete(y, 0, comment, (size_t)comment_len,
                             y, win->cursor.x, y, win->cursor.x - comment_len);
        }
        for (int i = 0; i < comment_len; i++) {
            sstr_delete_char(&row->chars, 0);
        }
        win->cursor.x -= comment_len;
        if (win->cursor.x < 0) win->cursor.x = 0;
    } else {
        /* Add comment */
        if (!undo_is_applying()) {
            undo_push_insert(y, 0, comment, (size_t)comment_len,
                             y, win->cursor.x, y, win->cursor.x + comment_len);
        }
        for (int i = comment_len - 1; i >= 0; i--) {
            sstr_insert_char(&row->chars, 0, comment[i]);
        }
        win->cursor.x += comment_len;
    }

    buf_row_update(row);
    buf->dirty++;

    if (!undo_is_applying()) {
        undo_commit_group();
    }
}

/*** Navigation helpers ***/

void buf_goto_line(int line_num) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win)) return;

    /* Convert from 1-indexed to 0-indexed */
    line_num--;

    if (line_num < 0) line_num = 0;
    if (line_num >= buf->num_rows) line_num = buf->num_rows - 1;

    win->cursor.y = line_num;
    win->cursor.x = 0;
}

/* Determine if a byte is part of a word (ASCII letters/digits/underscore, or non-ASCII byte) */
static inline int _is_word_byte(unsigned char b) {
    return (b == '_' || (b >= '0' && b <= '9') || (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z') || (b & 0x80));
}

/* UTF-8 helpers for codepoint-wise word operations */
static int _utf8_cp_start(const char *s, int len, int idx) {
    if (idx < 0) return 0;
    if (idx > len) idx = len;
    while (idx > 0 && ((unsigned char)s[idx] & 0xC0) == 0x80) {
        idx--;
    }
    return idx;
}

static int _utf8_next_cp(const char *s, int len, int idx) {
    if (idx < 0) idx = 0;
    if (idx >= len) return len;
    unsigned char c = (unsigned char)s[idx];
    int adv = 1;
    if ((c & 0x80) == 0) adv = 1;
    else if ((c & 0xE0) == 0xC0) adv = 2;
    else if ((c & 0xF0) == 0xE0) adv = 3;
    else if ((c & 0xF8) == 0xF0) adv = 4;
    if (idx + adv > len) adv = len - idx;
    return idx + adv;
}

static int _utf8_prev_cp(const char *s, int len, int idx) {
    if (idx <= 0) return -1;
    if (idx > len) idx = len;
    idx--; /* move to previous byte */
    while (idx > 0 && ((unsigned char)s[idx] & 0xC0) == 0x80) {
        idx--;
    }
    return idx;
}

static int _is_word_cp(const char *s, int len, int idx) {
    if (idx < 0 || idx >= len) return 0;
    unsigned char b = (unsigned char)s[idx];
    return _is_word_byte(b);
}

int buf_get_line_under_cursor(SizedStr *out) {
    Buffer *buf = buf_cur(); Window *win = window_cur(); if (!PTR_VALID(buf) || !PTR_VALID(win) || !PTR_VALID(out)) return 0;
    if (!BOUNDS_CHECK(win->cursor.y, buf->num_rows)) return 0;
    Row *row = &buf->rows[win->cursor.y];
    sstr_free(out); *out = sstr_from(row->chars.data, row->chars.len);
    return 1;
}

int buf_get_word_under_cursor(SizedStr *out) {
    Buffer *buf = buf_cur(); Window *win = window_cur(); if (!PTR_VALID(buf) || !PTR_VALID(win) || !PTR_VALID(out)) return 0;
    if (!BOUNDS_CHECK(win->cursor.y, buf->num_rows)) return 0;
    Row *row = &buf->rows[win->cursor.y];
    if (row->chars.len == 0) return 0;
    int sx = 0, ex = 0;
    if (!buf_get_word_range(&sx, &ex)) return 0;
    if (ex <= sx) return 0;
    sstr_free(out); *out = sstr_from(row->chars.data + sx, (size_t)(ex - sx));
    return 1;
}

int buf_get_paragraph_under_cursor(SizedStr *out) {
    Buffer *buf = buf_cur(); Window *win = window_cur(); if (!PTR_VALID(buf) || !PTR_VALID(win) || !PTR_VALID(out)) return 0;
    if (buf->num_rows == 0) return 0;
    int y = win->cursor.y; if (y < 0) y = 0; if (y >= buf->num_rows) y = buf->num_rows - 1;
    int start = y; while (start > 0) {
        const Row *r = &buf->rows[start - 1];
        int blank = 1; for (size_t i = 0; i < r->chars.len; i++) { char c = r->chars.data[i]; if (!(c == ' ' || c == '\t')) { blank = 0; break; } }
        if (blank) break; start--;
    }
    int end = y; while (end + 1 < buf->num_rows) {
        const Row *r = &buf->rows[end + 1];
        int blank = 1; for (size_t i = 0; i < r->chars.len; i++) { char c = r->chars.data[i]; if (!(c == ' ' || c == '\t')) { blank = 0; break; } }
        if (blank) break; end++;
    }
    sstr_free(out); *out = sstr_new();
    for (int i = start; i <= end; i++) {
        Row *r = &buf->rows[i];
        sstr_append(out, r->chars.data, r->chars.len);
        if (i != end) sstr_append_char(out, '\n');
    }
    return 1;
}

int buf_get_word_range(int *start_x, int *end_x) {
    Buffer *buf = buf_cur(); Window *win = window_cur(); if (!PTR_VALID(buf) || !PTR_VALID(win)) return 0;
    if (!BOUNDS_CHECK(win->cursor.y, buf->num_rows)) return 0;
    Row *row = &buf->rows[win->cursor.y]; if (row->chars.len == 0) return 0;

    const char *s = row->chars.data;
    int len = (int)row->chars.len;
    int cx = win->cursor.x;
    if (cx >= len) cx = len - 1;
    if (cx < 0) cx = 0;

    int i = _utf8_cp_start(s, len, cx);
    if (!_is_word_cp(s, len, i)) {
        int j = _utf8_prev_cp(s, len, i);
        while (j >= 0 && !_is_word_cp(s, len, j)) {
            j = _utf8_prev_cp(s, len, j);
        }
        if (j < 0) return 0;
        i = j;
    }

    int sx = i;
    int ex = _utf8_next_cp(s, len, i);
    while (ex < len && _is_word_cp(s, len, ex)) {
        ex = _utf8_next_cp(s, len, ex);
    }

    if (sx >= ex) return 0;
    if (start_x) *start_x = sx;
    if (end_x) *end_x = ex;
    return 1;
}

int buf_get_paragraph_range(int *start_y, int *end_y) {
    Buffer *buf = buf_cur(); Window *win = window_cur(); if (!PTR_VALID(buf) || !PTR_VALID(win)) return 0;
    if (buf->num_rows == 0) return 0; int y = win->cursor.y; if (y < 0) y = 0; if (y >= buf->num_rows) y = buf->num_rows - 1;
    int sy = y; while (sy > 0) {
        const Row *r = &buf->rows[sy - 1]; int blank = 1; for (size_t i = 0; i < r->chars.len; i++) { char c = r->chars.data[i]; if (!(c == ' ' || c == '\t')) { blank = 0; break; } }
        if (blank) break; sy--;
    }
    int ey = y; while (ey + 1 < buf->num_rows) {
        const Row *r = &buf->rows[ey + 1]; int blank = 1; for (size_t i = 0; i < r->chars.len; i++) { char c = r->chars.data[i]; if (!(c == ' ' || c == '\t')) { blank = 0; break; } }
        if (blank) break; ey++;
    }
    if (start_y) *start_y = sy; if (end_y) *end_y = ey; return 1;
}

static void buf_yank_range(int sy, int sx, int ey, int ex) {
    Buffer *buf = buf_cur(); if (!PTR_VALID(buf)) return;
    sstr_free(&E.clipboard); E.clipboard = sstr_new();
    if (sy == ey) {
        if (sy >= 0 && sy < buf->num_rows) {
            Row *r = &buf->rows[sy]; if (sx < 0) sx = 0; if (ex > (int)r->chars.len) ex = (int)r->chars.len; if (ex > sx)
                sstr_append(&E.clipboard, r->chars.data + sx, (size_t)(ex - sx));
        }
    } else {
        for (int y = sy; y <= ey; y++) {
            Row *r = &buf->rows[y];
            int lx = (y == sy) ? sx : 0;
            int rx = (y == ey) ? ex : (int)r->chars.len;
            if (lx < 0) lx = 0; if (rx > (int)r->chars.len) rx = (int)r->chars.len;
            if (rx > lx) sstr_append(&E.clipboard, r->chars.data + lx, (size_t)(rx - lx));
            if (y != ey) sstr_append_char(&E.clipboard, '\n');
        }
    }
    /* Update registers: yank '0' and unnamed */
    regs_set_yank(E.clipboard.data, E.clipboard.len);
}

static void buf_delete_range(int sy, int sx, int ey, int ex) {
    Buffer *buf = buf_cur(); Window *win = window_cur(); if (!PTR_VALID(buf) || !PTR_VALID(win)) return;
    if (sy > ey || (sy == ey && sx >= ex)) return;
    /* Capture to clipboard first */
    buf_yank_range(sy, sx, ey, ex);
    /* Record undo as a single, self-contained group so that a
     * single undo restores exactly this deleted region. */
    if (!undo_is_applying()) {
        int cy_before = win->cursor.y;
        int cx_before = win->cursor.x;
        undo_begin_group();
        undo_push_delete(sy, sx, E.clipboard.data, E.clipboard.len,
                         cy_before, cx_before, sy, sx);
        undo_commit_group();
    }
    /* Perform deletion on the buffer */
    if (sy == ey) {
        if (sy < 0 || sy >= buf->num_rows) return; Row *row = &buf->rows[sy];
        /* shift left */
        if (ex > (int)row->chars.len) ex = (int)row->chars.len; if (sx < 0) sx = 0; if (ex < sx) ex = sx;
        size_t tail = row->chars.len - ex;
        memmove(row->chars.data + sx, row->chars.data + ex, tail);
        row->chars.len -= (ex - sx); row->chars.data[row->chars.len] = '\0'; buf_row_update(row);
        win->cursor.x = sx;
    } else {
        /* Delete part of first line */
        Row *first = &buf->rows[sy]; if (sx > (int)first->chars.len) sx = (int)first->chars.len; first->chars.len = sx; first->chars.data[sx] = '\0'; buf_row_update(first);
        /* Delete middle lines */
        for (int y = ey - 1; y > sy; y--) buf_row_del_in(buf, y);
        /* Delete prefix of last line and merge */
        Row *last = &buf->rows[sy + 1]; int lrx = ex; if (lrx < 0) lrx = 0; if (lrx > (int)last->chars.len) lrx = (int)last->chars.len;
        SizedStr tail = sstr_from(last->chars.data + lrx, last->chars.len - lrx);
        buf_row_del_in(buf, sy + 1);
        buf_row_append_in(buf, first, &tail); sstr_free(&tail);
        win->cursor.y = sy; win->cursor.x = sx;
    }
}

void buf_yank_word(void) {
    int sx=0, ex=0; 
    if (!buf_get_word_range(&sx,&ex)) return; 
    int y = window_cur()->cursor.y; 
    buf_yank_range(y, sx, y, ex); 
    ed_set_status_message("yanked word"); }

void buf_delete_inner_word(void) {
    int sx=0, ex=0; 
    if (!buf_get_word_range(&sx,&ex)) return; 
    int y = window_cur()->cursor.y; 
    buf_delete_range(y, sx, y, ex); 
    ed_set_status_message("deleted inner word"); 
}

void buf_delete_word_forward(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win)) return;
    if (!BOUNDS_CHECK(win->cursor.y, buf->num_rows)) return;

    Row *row = &buf->rows[win->cursor.y];
    if (row->chars.len == 0) return;

    int cx = win->cursor.x;
    int n = (int)row->chars.len;

    /* If at or past end of line, do nothing */
    if (cx >= n) return;

    /* Find end of current word */
    int ex = cx;
    unsigned char first_char = (unsigned char)row->chars.data[cx];
    int is_word_char = _is_word_byte(first_char);

    if (is_word_char) {
        /* Delete to end of word */
        while (ex < n && _is_word_byte((unsigned char)row->chars.data[ex])) {
            ex++;
        }
    } else {
        /* On whitespace/punctuation, delete to next word start */
        while (ex < n && !_is_word_byte((unsigned char)row->chars.data[ex])) {
            ex++;
        }
    }

    if (ex > cx) {
        buf_delete_range(win->cursor.y, cx, win->cursor.y, ex);
        ed_set_status_message("deleted word forward");
    }
}

void buf_delete_word_backward(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win)) return;
    if (!BOUNDS_CHECK(win->cursor.y, buf->num_rows)) return;

    Row *row = &buf->rows[win->cursor.y];
    int cx = win->cursor.x;

    /* If at start of line, do nothing (could extend to join with previous line) */
    if (cx == 0) return;

    /* Find start of current word or whitespace */
    int sx = cx - 1;

    /* Check what we're on */
    unsigned char char_before = (unsigned char)row->chars.data[sx];
    int is_word_char = _is_word_byte(char_before);

    if (is_word_char) {
        /* Delete back through word */
        while (sx > 0 && _is_word_byte((unsigned char)row->chars.data[sx - 1])) {
            sx--;
        }
    } else {
        /* On whitespace/punctuation, delete back through it */
        while (sx > 0 && !_is_word_byte((unsigned char)row->chars.data[sx - 1])) {
            sx--;
        }
    }

    if (sx < cx) {
        buf_delete_range(win->cursor.y, sx, win->cursor.y, cx);
        win->cursor.x = sx; /* Position cursor at deletion point */
        ed_set_status_message("deleted word backward");
    }
}

void buf_yank_paragraph(void) {
    int sy=0, ey=0; if (!buf_get_paragraph_range(&sy,&ey)) return; buf_yank_range(sy, 0, ey, window_cur()->cursor.x /* ignored for full lines */); ed_set_status_message("yanked paragraph"); }

void buf_delete_paragraph(void) {
    int sy=0, ey=0; if (!buf_get_paragraph_range(&sy,&ey)) return; buf_delete_range(sy, 0, ey, (int)buf_cur()->rows[ey].chars.len); ed_set_status_message("deleted paragraph"); }

void buf_move_cursor_key(int key) {
    Buffer *buf = buf_cur(); Window *win = window_cur(); if (!PTR_VALID(buf) || !PTR_VALID(win)) return;
    Row *row = (win->cursor.y >= 0 && win->cursor.y < buf->num_rows) ? &buf->rows[win->cursor.y] : NULL;
    switch (key) {
        case KEY_ARROW_LEFT:
        case 'h':
            if (row) {
                int rx = buf_row_cx_to_rx(row, win->cursor.x);
                if (rx > 0) {
                    win->cursor.x = buf_row_rx_to_cx(row, rx - 1);
                } else if (win->cursor.y > 0) {
                    win->cursor.y--;
                    Row *pr = &buf->rows[win->cursor.y];
                    int prcols = buf_row_cx_to_rx(pr, (int)pr->chars.len);
                    win->cursor.x = buf_row_rx_to_cx(pr, prcols);
                }
            }
            break;
        case KEY_ARROW_DOWN:
        case 'j':
            if (win->wrap) {
                int gutter = 0;
                if (win->gutter_mode == 2) {
                    gutter = win->gutter_fixed_width; if (gutter < 0) gutter = 0;
                } else if (!(win->gutter_mode == 0 && !E.show_line_numbers)) {
                    int maxline = buf->num_rows;
                    if (E.relative_line_numbers) {
                        int maxrel = win->height;
                        if (maxrel < 1) maxrel = 1;
                        maxline = maxrel;
                    }
                    gutter = 0;
                    int tmp = maxline;
                    while (tmp > 0) { gutter++; tmp /= 10; }
                    if (gutter < 2) gutter = 2;
                }
                int margin = gutter ? (gutter + 1) : 0;
                int content_cols = win->width - margin;
                if (content_cols <= 0) content_cols = 1;
                int vis_col = 0;
                int cur_vis = cursor_visual_position(buf, win, content_cols, &vis_col);
                int total_vis = buffer_total_visual_rows(buf, content_cols);
                if (cur_vis < total_vis - 1) {
                    int target = cur_vis + 1;
                    cursor_from_visual(buf, win, target, content_cols, vis_col);
                }
            } else {
                if (win->cursor.y < buf->num_rows - 1) win->cursor.y++;
            }
            break;
        case KEY_ARROW_UP:
        case 'k':
            if (win->wrap) {
                int gutter = 0;
                if (win->gutter_mode == 2) {
                    gutter = win->gutter_fixed_width; if (gutter < 0) gutter = 0;
                } else if (!(win->gutter_mode == 0 && !E.show_line_numbers)) {
                    int maxline = buf->num_rows;
                    if (E.relative_line_numbers) {
                        int maxrel = win->height;
                        if (maxrel < 1) maxrel = 1;
                        maxline = maxrel;
                    }
                    gutter = 0;
                    int tmp = maxline;
                    while (tmp > 0) { gutter++; tmp /= 10; }
                    if (gutter < 2) gutter = 2;
                }
                int margin = gutter ? (gutter + 1) : 0;
                int content_cols = win->width - margin;
                if (content_cols <= 0) content_cols = 1;
                int vis_col = 0;
                int cur_vis = cursor_visual_position(buf, win, content_cols, &vis_col);
                if (cur_vis > 0) {
                    int target = cur_vis - 1;
                    cursor_from_visual(buf, win, target, content_cols, vis_col);
                }
            } else {
                if (win->cursor.y > 0) win->cursor.y--;
            }
            break;
        case KEY_ARROW_RIGHT:
        case 'l':
            if (row) {
                int rx = buf_row_cx_to_rx(row, win->cursor.x);
                int rcols = buf_row_cx_to_rx(row, (int)row->chars.len);
                if (rx < rcols) {
                    win->cursor.x = buf_row_rx_to_cx(row, rx + 1);
                } else if (win->cursor.y < buf->num_rows - 1) {
                    win->cursor.y++;
                    win->cursor.x = 0;
                }
            }
            break;
    }
    row = (win->cursor.y >= 0 && win->cursor.y < buf->num_rows) ? &buf->rows[win->cursor.y] : NULL;
    int rowlen = row ? (int)row->chars.len : 0;
    if (win->cursor.x > rowlen) win->cursor.x = rowlen;
    if (win->cursor.x < 0) win->cursor.x = 0;
}

void buf_find_matching_bracket(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win)) return;
    cur_sync_from_window(buf, win);
    if (!BOUNDS_CHECK(buf->cursor.y, buf->num_rows)) { cur_sync_to_window(buf, win); return; }

    Row *row = &buf->rows[buf->cursor.y];
    if (buf->cursor.x >= (int)row->chars.len) return;

    char ch = row->chars.data[buf->cursor.x];
    char match;
    int direction;

    /* Determine bracket type and direction */
    if (ch == '(' || ch == '{' || ch == '[') {
        direction = 1;  /* Forward */
        if (ch == '(') match = ')';
        else if (ch == '{') match = '}';
        else match = ']';
    } else if (ch == ')' || ch == '}' || ch == ']') {
        direction = -1;  /* Backward */
        if (ch == ')') match = '(';
        else if (ch == '}') match = '{';
        else match = '[';
    } else {
        return;  /* Not a bracket */
    }

    int depth = 1;
    int y = buf->cursor.y;
    int x = buf->cursor.x + direction;

    /* Search for matching bracket */
    while (y >= 0 && y < buf->num_rows) {
        row = &buf->rows[y];

        while ((direction == 1 && x < (int)row->chars.len) ||
               (direction == -1 && x >= 0)) {
            if (row->chars.data[x] == ch) {
                depth++;
            } else if (row->chars.data[x] == match) {
                depth--;
                if (depth == 0) {
                    /* Found match */
                    buf->cursor.y = y;
                    buf->cursor.x = x;
                    cur_sync_to_window(buf, win);
                    return;
                }
            }
            x += direction;
        }

        /* Move to next/previous line */
        y += direction;
        if (direction == 1) {
            x = 0;
        } else if (y >= 0 && y < buf->num_rows) {
            x = buf->rows[y].chars.len - 1;
        }
    }

    /* No match found */
    ed_set_status_message("No matching bracket found");
    cur_sync_to_window(buf, win);
}

/*** Selection helpers ***/

void buf_select_word(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur(); if (!PTR_VALID(buf) || !PTR_VALID(win)) return;
    int sx=0, ex=0; if (!buf_get_word_range(&sx,&ex)) return;
    win->cursor.x = ex;
}

void buf_select_line(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur(); if (!PTR_VALID(buf) || !PTR_VALID(win)) return;
    if (!BOUNDS_CHECK(win->cursor.y, buf->num_rows)) return;
    win->cursor.x = buf->rows[win->cursor.y].chars.len;
}

void buf_select_all(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur(); if (!PTR_VALID(buf) || !PTR_VALID(win)) return;
    win->cursor.y = buf->num_rows - 1; if (win->cursor.y < 0) win->cursor.y = 0;
    if (win->cursor.y < buf->num_rows) win->cursor.x = buf->rows[win->cursor.y].chars.len;
}

void buf_select_paragraph(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur(); if (!PTR_VALID(buf) || !PTR_VALID(win)) return;
    int sy=0, ey=0; if (!buf_get_paragraph_range(&sy,&ey)) return;
    win->cursor.y = ey; win->cursor.x = (int)buf->rows[ey].chars.len;
}

/*** Text-object deletion helpers ("da" / "di") ***/

static int bh_map_delim(int t, char *open, char *close) {
    switch (t) {
        case '(': case ')': *open = '('; *close = ')'; return 1;
        case '[': case ']': *open = '['; *close = ']'; return 1;
        case '{': case '}': *open = '{'; *close = '}'; return 1;
        case '<': case '>': *open = '<'; *close = '>'; return 1;
        case '"':          *open = '"'; *close = '"'; return 1;
        case '\'':         *open = '\''; *close = '\''; return 1;
        case '`':           *open = '`';  *close = '`';  return 1;
        default: return 0;
    }
}

static int bh_is_unescaped_quote(const Row *row, int x) {
    if (x < 0 || x >= (int)row->chars.len) return 0;
    int backslashes = 0;
    for (int i = x - 1; i >= 0 && row->chars.data[i] == '\\'; i--) backslashes++;
    return (backslashes % 2) == 0;
}

static int bh_find_enclosing_pair(Buffer *buf, char open, char close,
                                  int *oy, int *ox, int *cy, int *cx) {
    if (!buf || buf->num_rows == 0) return 0;
    int cur_y = buf->cursor.y;
    int cur_x = buf->cursor.x;

    int by = -1, bx = -1, found_open = 0;
    if (open == close) {
        for (int y = cur_y; y >= 0 && !found_open; y--) {
            Row *row = &buf->rows[y];
            int startx = (y == cur_y) ? (cur_x - 1) : (int)row->chars.len - 1;
            if (startx >= (int)row->chars.len) startx = (int)row->chars.len - 1;
            for (int x = startx; x >= 0; x--) {
                if (row->chars.data[x] == open && bh_is_unescaped_quote(row, x)) {
                    by = y; bx = x; found_open = 1; break;
                }
            }
        }
    } else {
        int depth = 0;
        for (int y = cur_y; y >= 0 && !found_open; y--) {
            Row *row = &buf->rows[y];
            int startx = (y == cur_y) ? (cur_x - 1) : (int)row->chars.len - 1;
            if (startx >= (int)row->chars.len) startx = (int)row->chars.len - 1;
            for (int x = startx; x >= 0; x--) {
                char c = row->chars.data[x];
                if (c == close) depth++;
                else if (c == open) {
                    if (depth == 0) { by = y; bx = x; found_open = 1; break; }
                    else depth--;
                }
            }
        }
    }
    if (!found_open) return 0;

    int fy = -1, fx = -1, found_close = 0;
    if (open == close) {
        for (int y = by; y < buf->num_rows && !found_close; y++) {
            Row *row = &buf->rows[y];
            int startx = (y == by) ? (bx + 1) : 0;
            for (int x = startx; x < (int)row->chars.len; x++) {
                if (row->chars.data[x] == close && bh_is_unescaped_quote(row, x)) {
                    fy = y; fx = x; found_close = 1; break;
                }
            }
        }
    } else {
        int depth = 0;
        for (int y = by; y < buf->num_rows && !found_close; y++) {
            Row *row = &buf->rows[y];
            int startx = (y == by) ? (bx + 1) : 0;
            for (int x = startx; x < (int)row->chars.len; x++) {
                char c = row->chars.data[x];
                if (c == open) depth++;
                else if (c == close) {
                    if (depth == 0) { fy = y; fx = x; found_close = 1; break; }
                    else depth--;
                }
            }
        }
    }
    if (!found_close) return 0;

    *oy = by; *ox = bx; *cy = fy; *cx = fx;
    return 1;
}

static void bh_delete_range(Buffer *buf, int sy, int sx, int ey, int ex) {
    /* Backwards-compatible wrapper: convert inclusive end to the
     * exclusive range used by buf_delete_range(). Used by older
     * callers; new code should prefer buf_delete_range directly. */
    if (!PTR_VALID(buf)) return;
    if (sy > ey || (sy == ey && sx > ex)) return;
    buf_delete_range(sy, sx, ey, ex + 1);
}

void buf_delete_around_char(void) {
    Buffer *buf = buf_cur();
    if (!PTR_VALID(buf)) return;
    ed_set_status_message("da: target?");
    int t = ed_read_key();
    char open = 0, close = 0;
    if (!bh_map_delim(t, &open, &close)) { ed_set_status_message("da: unsupported target"); return; }
    int oy, ox, cy, cx;
    if (!bh_find_enclosing_pair(buf, open, close, &oy, &ox, &cy, &cx)) { ed_set_status_message("da: no enclosing pair"); return; }
    /* Expand to trim surrounding whitespace outside the pair */
    int sy = oy, sx = ox;
    int ey = cy, ex = cx;
    /* Trim left spaces before opening on its line */
    if (sy >= 0 && sy < buf->num_rows) {
        Row *row = &buf->rows[sy];
        while (sx > 0 && isspace((unsigned char)row->chars.data[sx - 1])) sx--;
    }
    /* Trim right spaces after closing on its line */
    if (ey >= 0 && ey < buf->num_rows) {
        Row *row = &buf->rows[ey];
        while (ex + 1 < (int)row->chars.len && isspace((unsigned char)row->chars.data[ex + 1])) ex++;
    }
    /* bh_delete_range now delegates to buf_delete_range, which will
     * yank and record undo for the deleted text. */
    bh_delete_range(buf, sy, sx, ey, ex);
    ed_set_status_message("Deleted around %c", (open == close) ? open : close);
}

void buf_delete_inside_char(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win)) return;

    ed_set_status_message("di: target?");
    int t = ed_read_key();

    /* Word / paragraph / line text-objects */
    if (t == 'w') {
        buf_delete_inner_word();
        return;
    }
    if (t == 'p') {
        buf_delete_paragraph();
        return;
    }
    if (t == 'd') {
        if (!BOUNDS_CHECK(win->cursor.y, buf->num_rows)) return;
        Row *row = &buf->rows[win->cursor.y];
        int len = (int)row->chars.len;
        if (len <= 0) return;
        buf_delete_range(win->cursor.y, 0, win->cursor.y, len);
        ed_set_status_message("deleted line contents");
        return;
    }

    /* Bracket / quote text-objects */
    char open = 0, close = 0;
    if (!bh_map_delim(t, &open, &close)) {
        ed_set_status_message("di: unsupported target");
        return;
    }
    int oy, ox, cy, cx;
    if (!bh_find_enclosing_pair(buf, open, close, &oy, &ox, &cy, &cx)) {
        ed_set_status_message("di: no enclosing pair");
        return;
    }
    int sy = oy, sx = ox + 1;
    int ey = cy, ex = cx;
    if (ey < sy || (ey == sy && ex <= sx)) {
        ed_set_status_message("di: empty");
        return;
    }
    buf_delete_range(sy, sx, ey, ex);
    ed_set_status_message("Deleted inside %c", (open == close) ? open : close);
}

void buf_change_inside_char(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win)) return;

    ed_set_status_message("ci: target?");
    int t = ed_read_key();

    /* Word / paragraph / line text-objects */
    if (t == 'w') {
        buf_delete_inner_word();
        ed_set_mode(MODE_INSERT);
        return;
    }
    if (t == 'p') {
        buf_delete_paragraph();
        ed_set_mode(MODE_INSERT);
        return;
    }
    if (t == 'd') {
        if (!BOUNDS_CHECK(win->cursor.y, buf->num_rows)) return;
        Row *row = &buf->rows[win->cursor.y];
        int len = (int)row->chars.len;
        if (len > 0) {
            buf_delete_range(win->cursor.y, 0, win->cursor.y, len);
        }
        ed_set_mode(MODE_INSERT);
        ed_set_status_message("changed line contents");
        return;
    }

    /* Bracket / quote text-objects */
    char open = 0, close = 0;
    if (!bh_map_delim(t, &open, &close)) {
        ed_set_status_message("ci: unsupported target");
        return;
    }
    int oy, ox, cy, cx;
    if (!bh_find_enclosing_pair(buf, open, close, &oy, &ox, &cy, &cx)) {
        ed_set_status_message("ci: no enclosing pair");
        return;
    }
    int sy = oy, sx = ox + 1;
    int ey = cy, ex = cx;
    if (ey < sy || (ey == sy && ex <= sx)) {
        ed_set_status_message("ci: empty");
        return;
    }
    buf_delete_range(sy, sx, ey, ex);
    ed_set_mode(MODE_INSERT);
    ed_set_status_message("Changed inside %c", (open == close) ? open : close);
}
