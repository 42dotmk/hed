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
    if (PTR_VALID(buf) && PTR_VALID(win)) {
        win->cursor.x = buf->cursor.x;
        win->cursor.y = buf->cursor.y;
    }
}

/*
 * Macro to eliminate cursor sync boilerplate in helper functions.
 * Usage: CURSOR_OP({ code that modifies buf->cursor_x/y })
 */
#define CURSOR_OP(code)                                                        \
    do {                                                                       \
        Buffer *buf = buf_cur();                                               \
        Window *win = window_cur();                                            \
        if (!PTR_VALID(buf) || !PTR_VALID(win))                                \
            return;                                                            \
        cur_sync_from_window(buf, win);                                        \
        code cur_sync_to_window(buf, win);                                     \
    } while (0)

/* Visual-line helpers for soft-wrap navigation */
static int row_visual_height_buf(const Buffer *buf, int row_index,
                                 int content_cols, int wrap) {
    if (!wrap)
        return 1;
    if (!buf)
        return 1;
    if (row_index < 0 || row_index >= buf->num_rows)
        return 1;
    if (content_cols <= 0)
        return 1;
    const Row *row = &buf->rows[row_index];
    int rcols = buf_row_cx_to_rx(row, (int)row->chars.len);
    if (rcols <= 0)
        return 1;
    int h = (rcols + content_cols - 1) / content_cols;
    return h < 1 ? 1 : h;
}

static int cursor_visual_position(const Buffer *buf, const Window *win,
                                  int content_cols, int *out_vis_col) {
    if (!buf || !win || buf->num_rows <= 0) {
        if (out_vis_col)
            *out_vis_col = 0;
        return 0;
    }
    int cy = win->cursor.y;
    if (cy < 0)
        cy = 0;
    if (cy >= buf->num_rows)
        cy = buf->num_rows - 1;

    int visual = 0;
    for (int y = 0; y < cy; y++) {
        visual += row_visual_height_buf(buf, y, content_cols, 1);
    }
    const Row *row = &buf->rows[cy];
    int rx = buf_row_cx_to_rx(row, win->cursor.x);
    if (rx < 0)
        rx = 0;
    int h = row_visual_height_buf(buf, cy, content_cols, 1);
    int seg = rx / content_cols;
    if (seg >= h)
        seg = h - 1;
    int vis_col = rx % content_cols;
    if (out_vis_col)
        *out_vis_col = vis_col;
    return visual + seg;
}

static int buffer_total_visual_rows(const Buffer *buf, int content_cols) {
    if (!buf || buf->num_rows <= 0)
        return 0;
    int total = 0;
    for (int y = 0; y < buf->num_rows; y++) {
        total += row_visual_height_buf(buf, y, content_cols, 1);
    }
    return total;
}

static void cursor_from_visual(Buffer *buf, Window *win, int target_visual,
                               int content_cols, int vis_col) {
    if (!buf || !win || buf->num_rows <= 0) {
        win->cursor.y = 0;
        win->cursor.x = 0;
        return;
    }
    if (target_visual < 0)
        target_visual = 0;

    int y = 0;
    while (y < buf->num_rows) {
        int h = row_visual_height_buf(buf, y, content_cols, 1);
        if (target_visual < h)
            break;
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
        if (target_visual < 0)
            target_visual = 0;
    }

    Row *row = &buf->rows[y];
    int rcols = buf_row_cx_to_rx(row, (int)row->chars.len);
    if (rcols < 0)
        rcols = 0;
    if (content_cols <= 0)
        content_cols = 1;

    int seg_start = target_visual * content_cols;
    if (seg_start > rcols) {
        seg_start = (rcols / content_cols) * content_cols;
    }
    int rx = seg_start + vis_col;
    int seg_end = seg_start + content_cols;
    if (rx >= seg_end)
        rx = seg_end - 1;
    if (rx >= rcols && rcols > 0)
        rx = rcols - 1;
    if (rcols == 0)
        rx = 0;

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
        if (buf->cursor.y < 0)
            buf->cursor.y = 0;
        buf->cursor.x = 0;
    });
}

void buf_cursor_move_line_start(void) {
    CURSOR_OP({ buf->cursor.x = 0; });
}

void buf_cursor_move_line_end(void) {
    CURSOR_OP({
        if (BOUNDS_CHECK(buf->cursor.y, buf->num_rows)) {
            buf->cursor.x = buf->rows[buf->cursor.y].chars.len;
        }
    });
}

void buf_cursor_move_word_forward(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;

    TextSelection sel;
    if (textobj_to_word_end(buf, win->cursor.y, win->cursor.x, &sel)) {
        win->cursor.y = sel.end.line;
        win->cursor.x = sel.end.col;
    }
}

void buf_cursor_move_word_backward(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;

    TextSelection sel;
    if (textobj_to_word_start(buf, win->cursor.y, win->cursor.x, &sel)) {
        win->cursor.y = sel.start.line;
        win->cursor.x = sel.start.col;
    }
}

/*** Screen positioning helpers ***/

void buf_center_screen(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(win))
        return;
    /* Center current line in the middle of the current window (logical rows) */
    if (!buf || win->wrap)
        return;
    win->row_offset = win->cursor.y - (win->height / 2);
    if (win->row_offset < 0)
        win->row_offset = 0;
    int maxoff = buf->num_rows - win->height;
    if (win->row_offset > maxoff) {
        win->row_offset = maxoff;
        if (win->row_offset < 0)
            win->row_offset = 0;
    }
}

void buf_scroll_half_page_up(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;
    int half_page = E.screen_rows / 2;
    win->cursor.y -= half_page;
    if (win->cursor.y < 0)
        win->cursor.y = 0;
}

void buf_scroll_half_page_down(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;
    int half_page = E.screen_rows / 2;
    win->cursor.y += half_page;
    if (win->cursor.y >= buf->num_rows) {
        win->cursor.y = buf->num_rows - 1;
        if (win->cursor.y < 0)
            win->cursor.y = 0;
    }
}

void buf_scroll_page_up(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;
    win->cursor.y -= E.screen_rows;
    if (win->cursor.y < 0)
        win->cursor.y = 0;
}

void buf_scroll_page_down(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;
    win->cursor.y += E.screen_rows;
    if (win->cursor.y >= buf->num_rows) {
        win->cursor.y = buf->num_rows - 1;
        if (win->cursor.y < 0)
            win->cursor.y = 0;
    }
}

/*** Line operations helpers ***/

void buf_join_lines(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win) ||
        win->cursor.y >= buf->num_rows - 1)
        return;

    int y = win->cursor.y;
    Row *current = &buf->rows[y];
    Row *next = &buf->rows[y + 1];
    if (!PTR_VALID(current) || !PTR_VALID(next))
        return;

    int need_space = (current->chars.len > 0 &&
                      current->chars.data[current->chars.len - 1] != ' ');

    /* Optional space insertion at end of current line */
    if (need_space) {
        sstr_append_char(&current->chars, ' ');
        buf_row_update(current);
    }

    buf_row_append_in(buf, current, &next->chars);
    buf_row_del_in(buf, y + 1);
    buf->dirty++;
}

void buf_duplicate_line(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;
    if (!BOUNDS_CHECK(win->cursor.y, buf->num_rows))
        return;
    /* Implement as yank + paste so undo works naturally */
    buf_yank_line_in(buf);
    buf_paste_in(buf);
}

void buf_move_line_up(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win) || win->cursor.y == 0 ||
        buf->num_rows < 2)
        return;

    /* Swap with previous line */
    Row temp = buf->rows[win->cursor.y];
    buf->rows[win->cursor.y] = buf->rows[win->cursor.y - 1];
    buf->rows[win->cursor.y - 1] = temp;
    win->cursor.y--;
}

void buf_move_line_down(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win) ||
        win->cursor.y >= buf->num_rows - 1)
        return;

    /* Swap with next line */
    Row temp = buf->rows[win->cursor.y];
    buf->rows[win->cursor.y] = buf->rows[win->cursor.y + 1];
    buf->rows[win->cursor.y + 1] = temp;
    win->cursor.y++;
}

/*** Text manipulation helpers ***/

void buf_indent_line(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win) ||
        !BOUNDS_CHECK(win->cursor.y, buf->num_rows))
        return;
    if (buf->readonly) {
        ed_set_status_message("Buffer is read-only");
        return;
    }

    Row *row = &buf->rows[win->cursor.y];

    /* Insert TAB_STOP spaces at the beginning */
    for (int i = 0; i < TAB_STOP; i++) {
        sstr_insert_char(&row->chars, 0, ' ');
    }

    buf_row_update(row);
    win->cursor.x += TAB_STOP;
    buf->dirty++;
}

void buf_unindent_line(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win) ||
        !BOUNDS_CHECK(win->cursor.y, buf->num_rows))
        return;
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

    for (int i = 0; i < spaces_to_remove; i++) {
        sstr_delete_char(&row->chars, 0);
    }

    buf_row_update(row);
    win->cursor.x -= spaces_to_remove;
    if (win->cursor.x < 0)
        win->cursor.x = 0;
    buf->dirty++;
}

void buf_toggle_comment(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win) ||
        !BOUNDS_CHECK(win->cursor.y, buf->num_rows))
        return;
    if (buf->readonly) {
        ed_set_status_message("Buffer is read-only");
        return;
    }

    /* Determine comment string based on filetype */
    const char *comment = "// ";
    if (buf->filetype) {
        if (strcmp(buf->filetype, "python") == 0)
            comment = "# ";
        else if (strcmp(buf->filetype, "shell") == 0)
            comment = "# ";
        else if (strcmp(buf->filetype, "c") == 0)
            comment = "// ";
        else if (strcmp(buf->filetype, "cpp") == 0)
            comment = "// ";
        else if (strcmp(buf->filetype, "javascript") == 0)
            comment = "// ";
        else if (strcmp(buf->filetype, "rust") == 0)
            comment = "// ";
        else if (strcmp(buf->filetype, "go") == 0)
            comment = "// ";
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

    if (is_commented) {
        /* Remove comment */
        for (int i = 0; i < comment_len; i++) {
            sstr_delete_char(&row->chars, 0);
        }
        win->cursor.x -= comment_len;
        if (win->cursor.x < 0)
            win->cursor.x = 0;
    } else {
        for (int i = comment_len - 1; i >= 0; i--) {
            sstr_insert_char(&row->chars, 0, comment[i]);
        }
        win->cursor.x += comment_len;
    }

    buf_row_update(row);
    buf->dirty++;
}

/*** Navigation helpers ***/

void buf_goto_line(int line_num) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;

    /* Convert from 1-indexed to 0-indexed */
    line_num--;

    if (line_num < 0)
        line_num = 0;
    if (line_num >= buf->num_rows)
        line_num = buf->num_rows - 1;

    win->cursor.y = line_num;
    win->cursor.x = 0;
}

int buf_get_line_under_cursor(SizedStr *out) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win) || !PTR_VALID(out))
        return 0;
    TextSelection sel;
    if (!textobj_line(buf, win->cursor.y, win->cursor.x, &sel))
        return 0;
    Row *row = &buf->rows[sel.start.line];
    sstr_free(out);
    *out = sstr_from(row->chars.data + sel.start.col,
                     (size_t)(sel.end.col - sel.start.col));
    return 1;
}

int buf_get_word_under_cursor(SizedStr *out) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win) || !PTR_VALID(out))
        return 0;
    TextSelection sel;
    if (!textobj_word(buf, win->cursor.y, win->cursor.x, &sel))
        return 0;
    Row *row = &buf->rows[sel.start.line];
    sstr_free(out);
    *out = sstr_from(row->chars.data + sel.start.col,
                     (size_t)(sel.end.col - sel.start.col));
    return 1;
}

static int is_path_char(int c) {
    if (isalnum((unsigned char)c))
        return 1;
    switch (c) {
    case '/':
    case '.':
    case '_':
    case '-':
    case '~':
    case '+':
    case ':':
    case '\\':
        return 1;
    default:
        return 0;
    }
}

static int parse_number_slice(const char *start, size_t len) {
    char tmp[32];
    if (!start || len == 0)
        return 0;
    if (len >= sizeof(tmp))
        len = sizeof(tmp) - 1;
    memcpy(tmp, start, len);
    tmp[len] = '\0';
    return atoi(tmp);
}

static void strip_path_position(SizedStr *path, int *out_line, int *out_col) {
    if (out_line)
        *out_line = 0;
    if (out_col)
        *out_col = 0;
    if (!path || !path->data || path->len == 0)
        return;

    size_t len = path->len;
    size_t num_end = len;

    /* Look for a trailing :number (column or line). */
    while (num_end > 0 && isdigit((unsigned char)path->data[num_end - 1])) {
        num_end--;
    }
    if (num_end == len || num_end == 0)
        return;
    if (path->data[num_end - 1] != ':')
        return;

    size_t last_colon = num_end - 1;
    int last_num = parse_number_slice(path->data + num_end, len - num_end);
    size_t path_end = last_colon;

    /* See if we have path:line:col by checking for another :number. */
    size_t num2_end = last_colon;
    while (num2_end > 0 && isdigit((unsigned char)path->data[num2_end - 1])) {
        num2_end--;
    }
    if (num2_end > 0 && path->data[num2_end - 1] == ':' &&
        num2_end < last_colon) {
        int line_num =
            parse_number_slice(path->data + num2_end, last_colon - num2_end);
        if (out_line)
            *out_line = line_num;
        if (out_col)
            *out_col = last_num;
        path_end = num2_end - 1;
    } else {
        if (out_line)
            *out_line = last_num;
    }

    if (path_end < path->len) {
        path->len = path_end;
        path->data[path_end] = '\0';
    }
}

int buf_get_path_under_cursor(SizedStr *out, int *out_line, int *out_col) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win) || !PTR_VALID(out))
        return 0;
    if (!BOUNDS_CHECK(win->cursor.y, buf->num_rows))
        return 0;

    Row *row = &buf->rows[win->cursor.y];
    if (row->chars.len == 0)
        return 0;

    if (out_line)
        *out_line = 0;
    if (out_col)
        *out_col = 0;

    int len = (int)row->chars.len;
    int cx = win->cursor.x;
    if (cx >= len)
        cx = len - 1;
    if (cx < 0)
        return 0;

    const char *s = row->chars.data;
    if (!is_path_char(s[cx])) {
        int left = cx - 1;
        while (left >= 0 && !is_path_char(s[left])) {
            left--;
        }
        if (left < 0 || !is_path_char(s[left]))
            return 0;
        cx = left;
    }

    int start = cx;
    int end = cx + 1;
    while (start > 0 && is_path_char(s[start - 1])) {
        start--;
    }
    while (end < len && is_path_char(s[end])) {
        end++;
    }
    if (end <= start)
        return 0;

    sstr_free(out);
    *out = sstr_from(s + start, (size_t)(end - start));
    if (!out->data || out->len == 0) {
        sstr_free(out);
        return 0;
    }

    strip_path_position(out, out_line, out_col);
    if (!out->data || out->len == 0) {
        sstr_free(out);
        return 0;
    }

    return 1;
}

int buf_get_paragraph_under_cursor(SizedStr *out) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win) || !PTR_VALID(out))
        return 0;
    TextSelection sel;
    if (!textobj_paragraph(buf, win->cursor.y, win->cursor.x, &sel))
        return 0;
    sstr_free(out);
    *out = sstr_new();
    for (int y = sel.start.line; y <= sel.end.line; y++) {
        Row *r = &buf->rows[y];
        int start_col = (y == sel.start.line) ? sel.start.col : 0;
        int end_col = (y == sel.end.line) ? sel.end.col : (int)r->chars.len;
        if (start_col < 0)
            start_col = 0;
        if (end_col > (int)r->chars.len)
            end_col = (int)r->chars.len;
        if (end_col > start_col) {
            sstr_append(out, r->chars.data + start_col,
                        (size_t)(end_col - start_col));
        }
        if (y != sel.end.line)
            sstr_append_char(out, '\n');
    }
    return 1;
}

int buf_get_word_range(int *start_x, int *end_x) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return 0;
    TextSelection sel;
    if (!textobj_word(buf, win->cursor.y, win->cursor.x, &sel))
        return 0;
    if (start_x)
        *start_x = sel.start.col;
    if (end_x)
        *end_x = sel.end.col;
    return 1;
}

int buf_get_paragraph_range(int *start_y, int *end_y) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return 0;
    TextSelection sel;
    if (!textobj_paragraph(buf, win->cursor.y, win->cursor.x, &sel))
        return 0;
    if (start_y)
        *start_y = sel.start.line;
    if (end_y)
        *end_y = sel.end.line;
    return 1;
}

static void buf_yank_range(int sy, int sx, int ey, int ex) {
    Buffer *buf = buf_cur();
    if (!PTR_VALID(buf))
        return;
    sstr_free(&E.clipboard);
    E.clipboard = sstr_new();
    if (sy == ey) {
        if (sy >= 0 && sy < buf->num_rows) {
            Row *r = &buf->rows[sy];
            if (sx < 0)
                sx = 0;
            if (ex > (int)r->chars.len)
                ex = (int)r->chars.len;
            if (ex > sx)
                sstr_append(&E.clipboard, r->chars.data + sx,
                            (size_t)(ex - sx));
        }
    } else {
        for (int y = sy; y <= ey; y++) {
            Row *r = &buf->rows[y];
            int lx = (y == sy) ? sx : 0;
            int rx = (y == ey) ? ex : (int)r->chars.len;
            if (lx < 0)
                lx = 0;
            if (rx > (int)r->chars.len)
                rx = (int)r->chars.len;
            if (rx > lx)
                sstr_append(&E.clipboard, r->chars.data + lx,
                            (size_t)(rx - lx));
            if (y != ey)
                sstr_append_char(&E.clipboard, '\n');
        }
    }
    /* Update registers: yank '0' and unnamed */
    regs_set_yank(E.clipboard.data, E.clipboard.len);
}

static void buf_delete_range(int sy, int sx, int ey, int ex) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;
    if (sy > ey || (sy == ey && sx >= ex))
        return;
    /* Capture to clipboard first */
    buf_yank_range(sy, sx, ey, ex);

    /* Perform deletion on the buffer */
    if (sy == ey) {
        if (sy < 0 || sy >= buf->num_rows)
            return;
        Row *row = &buf->rows[sy];
        /* shift left */
        if (ex > (int)row->chars.len)
            ex = (int)row->chars.len;
        if (sx < 0)
            sx = 0;
        if (ex < sx)
            ex = sx;
        size_t tail = row->chars.len - ex;
        memmove(row->chars.data + sx, row->chars.data + ex, tail);
        row->chars.len -= (ex - sx);
        row->chars.data[row->chars.len] = '\0';
        buf_row_update(row);
        win->cursor.x = sx;
    } else {
        /* Delete part of first line */
        Row *first = &buf->rows[sy];
        if (sx > (int)first->chars.len)
            sx = (int)first->chars.len;
        first->chars.len = sx;
        first->chars.data[sx] = '\0';
        buf_row_update(first);
        /* Delete middle lines */
        for (int y = ey - 1; y > sy; y--)
            buf_row_del_in(buf, y);
        /* Delete prefix of last line and merge */
        Row *last = &buf->rows[sy + 1];
        int lrx = ex;
        if (lrx < 0)
            lrx = 0;
        if (lrx > (int)last->chars.len)
            lrx = (int)last->chars.len;
        SizedStr tail =
            sstr_from(last->chars.data + lrx, last->chars.len - lrx);
        buf_row_del_in(buf, sy + 1);
        buf_row_append_in(buf, first, &tail);
        sstr_free(&tail);
        win->cursor.y = sy;
        win->cursor.x = sx;
    }
}

/*
 * Selection-based operations - unified interface for delete/yank/change
 */

void buf_delete_selection(TextSelection *sel) {
    if (!sel)
        return;
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;

    /* Special case: whole-line deletion (dd command)
     * Detected when end=(start.line+1, 0) and start.col=0 */
    if (sel->end.line == sel->start.line + 1 && sel->end.col == 0 &&
        sel->start.col == 0) {
        /* Use existing line deletion logic which properly handles row removal */
        buf_delete_line_in(buf);
        return;
    }

    /* Normal range deletion */
    buf_delete_range(sel->start.line, sel->start.col, sel->end.line,
                     sel->end.col);

    /* Update cursor to the position specified by the textobject */
    win->cursor.y = sel->cursor.line;
    win->cursor.x = sel->cursor.col;
}

void buf_yank_selection(TextSelection *sel) {
    if (!sel)
        return;

    buf_yank_range(sel->start.line, sel->start.col, sel->end.line,
                   sel->end.col);
    ed_set_status_message("Yanked");
}

void buf_change_selection(TextSelection *sel) {
    buf_delete_selection(sel);
    ed_set_mode(MODE_INSERT);
}

void buf_yank_word(void) {
    int sx = 0, ex = 0;
    if (!buf_get_word_range(&sx, &ex))
        return;
    int y = window_cur()->cursor.y;
    buf_yank_range(y, sx, y, ex);
    ed_set_status_message("yanked word");
}

void buf_delete_inner_word(void) {
    int sx = 0, ex = 0;
    if (!buf_get_word_range(&sx, &ex))
        return;
    int y = window_cur()->cursor.y;
    buf_delete_range(y, sx, y, ex);
    ed_set_status_message("deleted inner word");
}

void buf_delete_word_forward(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;
    TextSelection sel;
    if (!textobj_to_word_end(buf, win->cursor.y, win->cursor.x, &sel))
        return;
    buf_delete_range(sel.start.line, sel.start.col, sel.end.line, sel.end.col);
    win->cursor.y = sel.start.line;
    win->cursor.x = sel.start.col;
    ed_set_status_message("deleted word forward");
}

void buf_delete_word_backward(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;
    TextSelection sel;
    if (!textobj_to_word_start(buf, win->cursor.y, win->cursor.x, &sel))
        return;
    buf_delete_range(sel.start.line, sel.start.col, sel.end.line, sel.end.col);
    win->cursor.y = sel.start.line;
    win->cursor.x = sel.start.col;
    ed_set_status_message("deleted word backward");
}

void buf_yank_paragraph(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;
    TextSelection sel;
    if (!textobj_paragraph(buf, win->cursor.y, win->cursor.x, &sel))
        return;
    buf_yank_range(sel.start.line, sel.start.col, sel.end.line, sel.end.col);
    ed_set_status_message("yanked paragraph");
}

void buf_delete_paragraph(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;
    TextSelection sel;
    if (!textobj_paragraph(buf, win->cursor.y, win->cursor.x, &sel))
        return;
    buf_delete_range(sel.start.line, sel.start.col, sel.end.line, sel.end.col);
    win->cursor.y = sel.start.line;
    win->cursor.x = sel.start.col;
    ed_set_status_message("deleted paragraph");
}

/*
 * Change commands - delete and enter insert mode
 */

void buf_change_word_forward(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;

    TextSelection sel;
    if (!textobj_to_word_end(buf, win->cursor.y, win->cursor.x, &sel))
        return;

    buf_change_selection(&sel);
}

void buf_change_word_backward(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;

    TextSelection sel;
    if (!textobj_to_word_start(buf, win->cursor.y, win->cursor.x, &sel))
        return;

    buf_change_selection(&sel);
}

void buf_change_inner_word(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;

    TextSelection sel;
    if (!textobj_word(buf, win->cursor.y, win->cursor.x, &sel))
        return;

    buf_change_selection(&sel);
}

void buf_change_line(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;

    TextSelection sel;
    if (!textobj_line(buf, win->cursor.y, win->cursor.x, &sel))
        return;

    buf_change_selection(&sel);
}

void buf_change_paragraph(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;

    TextSelection sel;
    if (!textobj_paragraph(buf, win->cursor.y, win->cursor.x, &sel))
        return;

    buf_change_selection(&sel);
}

void buf_change_around_char(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;

    ed_set_status_message("ca: enter target character");
    ed_render_frame();
    int c = ed_read_key();

    char open, close;
    if (c == '(' || c == ')')
        open = '(', close = ')';
    else if (c == '{' || c == '}')
        open = '{', close = '}';
    else if (c == '[' || c == ']')
        open = '[', close = ']';
    else if (c == '<' || c == '>')
        open = '<', close = '>';
    else if (c == '"')
        open = close = '"';
    else if (c == '\'')
        open = close = '\'';
    else if (c == '`')
        open = close = '`';
    else {
        ed_set_status_message("ca: invalid delimiter");
        return;
    }

    TextSelection sel;
    if (!textobj_brackets_with(buf, win->cursor.y, win->cursor.x, open, close,
                                true, &sel)) {
        ed_set_status_message("ca: no enclosing pair");
        return;
    }

    buf_change_selection(&sel);
}

void buf_move_cursor_key(int key) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;
    Row *row = (win->cursor.y >= 0 && win->cursor.y < buf->num_rows)
                   ? &buf->rows[win->cursor.y]
                   : NULL;
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
                gutter = win->gutter_fixed_width;
                if (gutter < 0)
                    gutter = 0;
            } else if (!(win->gutter_mode == 0 && !E.show_line_numbers)) {
                int maxline = buf->num_rows;
                if (E.relative_line_numbers) {
                    int maxrel = win->height;
                    if (maxrel < 1)
                        maxrel = 1;
                    maxline = maxrel;
                }
                gutter = 0;
                int tmp = maxline;
                while (tmp > 0) {
                    gutter++;
                    tmp /= 10;
                }
                if (gutter < 2)
                    gutter = 2;
            }
            int margin = gutter ? (gutter + 1) : 0;
            int content_cols = win->width - margin;
            if (content_cols <= 0)
                content_cols = 1;
            int vis_col = 0;
            int cur_vis =
                cursor_visual_position(buf, win, content_cols, &vis_col);
            int total_vis = buffer_total_visual_rows(buf, content_cols);
            if (cur_vis < total_vis - 1) {
                int target = cur_vis + 1;
                cursor_from_visual(buf, win, target, content_cols, vis_col);
            }
        } else {
            if (win->cursor.y < buf->num_rows - 1)
                win->cursor.y++;
        }
        break;
    case KEY_ARROW_UP:
    case 'k':
        if (win->wrap) {
            int gutter = 0;
            if (win->gutter_mode == 2) {
                gutter = win->gutter_fixed_width;
                if (gutter < 0)
                    gutter = 0;
            } else if (!(win->gutter_mode == 0 && !E.show_line_numbers)) {
                int maxline = buf->num_rows;
                if (E.relative_line_numbers) {
                    int maxrel = win->height;
                    if (maxrel < 1)
                        maxrel = 1;
                    maxline = maxrel;
                }
                gutter = 0;
                int tmp = maxline;
                while (tmp > 0) {
                    gutter++;
                    tmp /= 10;
                }
                if (gutter < 2)
                    gutter = 2;
            }
            int margin = gutter ? (gutter + 1) : 0;
            int content_cols = win->width - margin;
            if (content_cols <= 0)
                content_cols = 1;
            int vis_col = 0;
            int cur_vis =
                cursor_visual_position(buf, win, content_cols, &vis_col);
            if (cur_vis > 0) {
                int target = cur_vis - 1;
                cursor_from_visual(buf, win, target, content_cols, vis_col);
            }
        } else {
            if (win->cursor.y > 0)
                win->cursor.y--;
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
    row = (win->cursor.y >= 0 && win->cursor.y < buf->num_rows)
              ? &buf->rows[win->cursor.y]
              : NULL;
    int rowlen = row ? (int)row->chars.len : 0;
    if (win->cursor.x > rowlen)
        win->cursor.x = rowlen;
    if (win->cursor.x < 0)
        win->cursor.x = 0;
}

void buf_find_matching_bracket(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;
    cur_sync_from_window(buf, win);
    if (!BOUNDS_CHECK(buf->cursor.y, buf->num_rows)) {
        cur_sync_to_window(buf, win);
        return;
    }

    Row *row = &buf->rows[buf->cursor.y];
    if (buf->cursor.x >= (int)row->chars.len)
        return;

    char ch = row->chars.data[buf->cursor.x];
    char match;
    int direction;

    /* Determine bracket type and direction */
    if (ch == '(' || ch == '{' || ch == '[') {
        direction = 1; /* Forward */
        if (ch == '(')
            match = ')';
        else if (ch == '{')
            match = '}';
        else
            match = ']';
    } else if (ch == ')' || ch == '}' || ch == ']') {
        direction = -1; /* Backward */
        if (ch == ')')
            match = '(';
        else if (ch == '}')
            match = '{';
        else
            match = '[';
    } else {
        return; /* Not a bracket */
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
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;
    int sx = 0, ex = 0;
    if (!buf_get_word_range(&sx, &ex))
        return;
    win->cursor.x = ex;
}

void buf_select_line(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;
    if (!BOUNDS_CHECK(win->cursor.y, buf->num_rows))
        return;
    win->cursor.x = buf->rows[win->cursor.y].chars.len;
}

void buf_select_all(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;
    win->cursor.y = buf->num_rows - 1;
    if (win->cursor.y < 0)
        win->cursor.y = 0;
    if (win->cursor.y < buf->num_rows)
        win->cursor.x = buf->rows[win->cursor.y].chars.len;
}

void buf_select_paragraph(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;
    TextSelection sel;
    if (!textobj_paragraph(buf, win->cursor.y, win->cursor.x, &sel))
        return;
    win->cursor.y = sel.end.line;
    win->cursor.x = sel.end.col;
}

/*** Text-object deletion helpers ("da" / "di") ***/

static int map_delim_key(int t, char *open, char *close) {
    switch (t) {
    case '(':
    case ')':
        *open = '(';
        *close = ')';
        return 1;
    case '[':
    case ']':
        *open = '[';
        *close = ']';
        return 1;
    case '{':
    case '}':
        *open = '{';
        *close = '}';
        return 1;
    case '<':
    case '>':
        *open = '<';
        *close = '>';
        return 1;
    case '"':
        *open = '"';
        *close = '"';
        return 1;
    case '\'':
        *open = '\'';
        *close = '\'';
        return 1;
    case '`':
        *open = '`';
        *close = '`';
        return 1;
    default:
        return 0;
    }
}

void buf_delete_around_char(void) {
    Buffer *buf = buf_cur();
    if (!PTR_VALID(buf))
        return;
    ed_set_status_message("da: target?");
    int t = ed_read_key();
    char open = 0, close = 0;
    if (!map_delim_key(t, &open, &close)) {
        ed_set_status_message("da: unsupported target");
        return;
    }
    Window *win = window_cur();
    if (!PTR_VALID(win))
        return;
    TextSelection sel;
    if (!textobj_brackets_with(buf, win->cursor.y, win->cursor.x, open, close,
                               true, &sel)) {
        ed_set_status_message("da: no enclosing pair");
        return;
    }
    buf_delete_range(sel.start.line, sel.start.col, sel.end.line, sel.end.col);
    win->cursor.y = sel.start.line;
    win->cursor.x = sel.start.col;
    ed_set_status_message("Deleted around %c", (open == close) ? open : close);
}

void buf_delete_inside_char(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;

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
        if (!BOUNDS_CHECK(win->cursor.y, buf->num_rows))
            return;
        Row *row = &buf->rows[win->cursor.y];
        int len = (int)row->chars.len;
        if (len <= 0)
            return;
        buf_delete_range(win->cursor.y, 0, win->cursor.y, len);
        ed_set_status_message("deleted line contents");
        return;
    }

    /* Bracket / quote text-objects */
    char open = 0, close = 0;
    if (!map_delim_key(t, &open, &close)) {
        ed_set_status_message("di: unsupported target");
        return;
    }
    TextSelection sel;
    if (!textobj_brackets_with(buf, win->cursor.y, win->cursor.x, open, close,
                               false, &sel)) {
        ed_set_status_message("di: no enclosing pair");
        return;
    }
    if (sel.end.line < sel.start.line ||
        (sel.end.line == sel.start.line && sel.end.col <= sel.start.col)) {
        ed_set_status_message("di: empty");
        return;
    }
    buf_delete_range(sel.start.line, sel.start.col, sel.end.line, sel.end.col);
    win->cursor.y = sel.start.line;
    win->cursor.x = sel.start.col;
    ed_set_status_message("Deleted inside %c", (open == close) ? open : close);
}

void buf_change_inside_char(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;

    ed_set_status_message("ci: target?");
    int t = ed_read_key();

    TextSelection sel;

    /* Word / paragraph / line text-objects */
    if (t == 'w') {
        if (!textobj_word(buf, win->cursor.y, win->cursor.x, &sel))
            return;
        buf_change_selection(&sel);
        return;
    }
    if (t == 'p') {
        if (!textobj_paragraph(buf, win->cursor.y, win->cursor.x, &sel))
            return;
        buf_change_selection(&sel);
        return;
    }
    if (t == 'd') {
        if (!textobj_line(buf, win->cursor.y, win->cursor.x, &sel))
            return;
        buf_change_selection(&sel);
        ed_set_status_message("changed line contents");
        return;
    }

    /* Bracket / quote text-objects */
    char open = 0, close = 0;
    if (!map_delim_key(t, &open, &close)) {
        ed_set_status_message("ci: unsupported target");
        return;
    }
    if (!textobj_brackets_with(buf, win->cursor.y, win->cursor.x, open, close,
                               false, &sel)) {
        ed_set_status_message("ci: no enclosing pair");
        return;
    }
    if (sel.end.line < sel.start.line ||
        (sel.end.line == sel.start.line && sel.end.col <= sel.start.col)) {
        ed_set_status_message("ci: empty");
        return;
    }
    buf_change_selection(&sel);
    ed_set_status_message("Changed inside %c", (open == close) ? open : close);
}
