#include "hed.h"

/*** Cursor movement helpers ***/

static inline void cur_sync_from_window(Buffer *buf, Window *win) {
    if (buf && win) { buf->cursor_x = win->cursor_x; buf->cursor_y = win->cursor_y; }
}
static inline void cur_sync_to_window(Buffer *buf, Window *win) {
    if (buf && win) { win->cursor_x = buf->cursor_x; win->cursor_y = buf->cursor_y; }
}

void buf_cursor_move_top(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!buf || !win) return;
    cur_sync_from_window(buf, win);
    buf->cursor_y = 0;
    buf->cursor_x = 0;
    cur_sync_to_window(buf, win);
}

void buf_cursor_move_bottom(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!buf || !win) return;
    cur_sync_from_window(buf, win);
    buf->cursor_y = buf->num_rows - 1;
    if (buf->cursor_y < 0) buf->cursor_y = 0;
    buf->cursor_x = 0;
    cur_sync_to_window(buf, win);
}

void buf_cursor_move_line_start(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!buf || !win) return;
    cur_sync_from_window(buf, win);
    buf->cursor_x = 0;
    cur_sync_to_window(buf, win);
}

void buf_cursor_move_line_end(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!buf || !win) return;
    cur_sync_from_window(buf, win);
    if (buf->cursor_y < buf->num_rows) {
        buf->cursor_x = buf->rows[buf->cursor_y].chars.len;
    }
    cur_sync_to_window(buf, win);
}

void buf_cursor_move_word_forward(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!buf || !win) return;
    cur_sync_from_window(buf, win);
    if (buf->cursor_y >= buf->num_rows) { cur_sync_to_window(buf, win); return; }

    Row *row = &buf->rows[buf->cursor_y];

    /* Skip current word */
    while (buf->cursor_x < (int)row->chars.len &&
           !isspace(row->chars.data[buf->cursor_x])) {
        buf->cursor_x++;
    }

    /* Skip whitespace */
    while (buf->cursor_x < (int)row->chars.len &&
           isspace(row->chars.data[buf->cursor_x])) {
        buf->cursor_x++;
    }

    /* If at end of line, move to next line */
    if (buf->cursor_x >= (int)row->chars.len &&
        buf->cursor_y < buf->num_rows - 1) {
        buf->cursor_y++;
        buf->cursor_x = 0;
    }
    cur_sync_to_window(buf, win);
}

void buf_cursor_move_word_backward(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!buf || !win) return;
    cur_sync_from_window(buf, win);

    /* If at start of line, go to end of previous line */
    if (buf->cursor_x == 0) {
        if (buf->cursor_y > 0) {
            buf->cursor_y--;
            if (buf->cursor_y < buf->num_rows) {
                buf->cursor_x = buf->rows[buf->cursor_y].chars.len;
            }
        }
        cur_sync_to_window(buf, win);
        return;
    }

    Row *row = &buf->rows[buf->cursor_y];
    buf->cursor_x--;

    /* Skip whitespace */
    while (buf->cursor_x > 0 && isspace(row->chars.data[buf->cursor_x])) {
        buf->cursor_x--;
    }

    /* Skip word */
    while (buf->cursor_x > 0 && !isspace(row->chars.data[buf->cursor_x - 1])) {
        buf->cursor_x--;
    }
    cur_sync_to_window(buf, win);
}

/*** Screen positioning helpers ***/

void buf_center_screen(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!win) return;
    /* Center current line in the middle of the current window */
    win->row_offset = win->cursor_y - (win->height / 2);
    if (win->row_offset < 0) win->row_offset = 0;
    int maxoff = buf->num_rows - win->height;
    if (win->row_offset > maxoff) {
        win->row_offset = maxoff;
        if (win->row_offset < 0) win->row_offset = 0;
    }
}

void buf_scroll_half_page_up(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!buf || !win) return;
    int half_page = E.screen_rows / 2;
    win->cursor_y -= half_page;
    if (win->cursor_y < 0) win->cursor_y = 0;
}

void buf_scroll_half_page_down(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!buf || !win) return;
    int half_page = E.screen_rows / 2;
    win->cursor_y += half_page;
    if (win->cursor_y >= buf->num_rows) {
        win->cursor_y = buf->num_rows - 1;
        if (win->cursor_y < 0) win->cursor_y = 0;
    }
}

void buf_scroll_page_up(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!buf || !win) return;
    win->cursor_y -= E.screen_rows;
    if (win->cursor_y < 0) win->cursor_y = 0;
}

void buf_scroll_page_down(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!buf || !win) return;
    win->cursor_y += E.screen_rows;
    if (win->cursor_y >= buf->num_rows) {
        win->cursor_y = buf->num_rows - 1;
        if (win->cursor_y < 0) win->cursor_y = 0;
    }
}

/*** Line operations helpers ***/

void buf_join_lines(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!buf || !win || win->cursor_y >= buf->num_rows - 1) return;

    /* Append next line to current line with a space */
    Row *current = &buf->rows[win->cursor_y];
    Row *next = &buf->rows[win->cursor_y + 1];

    /* Add space if current line doesn't end with one */
    if (current->chars.len > 0 &&
        current->chars.data[current->chars.len - 1] != ' ') {
        sstr_append_char(&current->chars, ' ');
    }

    /* Append next line */
    buf_row_append(current, &next->chars);

    /* Delete next line */
    buf_row_del(win->cursor_y + 1);
}

void buf_duplicate_line(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!buf || !win || win->cursor_y >= buf->num_rows) return;
    Row *row = &buf->rows[win->cursor_y];
    buf_row_insert(win->cursor_y + 1, row->chars.data, row->chars.len);
    win->cursor_y++;
}

void buf_move_line_up(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!buf || !win || win->cursor_y == 0 || buf->num_rows < 2) return;

    /* Swap with previous line */
    Row temp = buf->rows[win->cursor_y];
    buf->rows[win->cursor_y] = buf->rows[win->cursor_y - 1];
    buf->rows[win->cursor_y - 1] = temp;
    win->cursor_y--;
}

void buf_move_line_down(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!buf || !win || win->cursor_y >= buf->num_rows - 1) return;

    /* Swap with next line */
    Row temp = buf->rows[win->cursor_y];
    buf->rows[win->cursor_y] = buf->rows[win->cursor_y + 1];
    buf->rows[win->cursor_y + 1] = temp;
    win->cursor_y++;
}

/*** Text manipulation helpers ***/

void buf_indent_line(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!buf || !win || win->cursor_y >= buf->num_rows) return;

    Row *row = &buf->rows[win->cursor_y];

    /* Insert TAB_STOP spaces at the beginning */
    for (int i = 0; i < TAB_STOP; i++) {
        sstr_insert_char(&row->chars, 0, ' ');
    }

    buf_row_update(row);
    win->cursor_x += TAB_STOP;
    buf->dirty++;
}

void buf_unindent_line(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!buf || !win || win->cursor_y >= buf->num_rows) return;

    Row *row = &buf->rows[win->cursor_y];

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
    win->cursor_x -= spaces_to_remove;
    if (win->cursor_x < 0) win->cursor_x = 0;
    buf->dirty++;
}

void buf_toggle_comment(void) {
    Buffer *buf = buf_cur();
    if (!buf || buf->cursor_y >= buf->num_rows) return;

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

    Row *row = &buf->rows[buf->cursor_y];
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
        Window *win = window_cur(); if (win) { win->cursor_x -= comment_len; if (win->cursor_x < 0) win->cursor_x = 0; }
    } else {
        /* Add comment */
        for (int i = comment_len - 1; i >= 0; i--) {
            sstr_insert_char(&row->chars, 0, comment[i]);
        }
        Window *win = window_cur(); if (win) { win->cursor_x += comment_len; }
    }

    buf_row_update(row);
    buf->dirty++;
}

/*** Navigation helpers ***/

void buf_goto_line(int line_num) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!buf || !win) return;

    /* Convert from 1-indexed to 0-indexed */
    line_num--;

    if (line_num < 0) line_num = 0;
    if (line_num >= buf->num_rows) line_num = buf->num_rows - 1;

    win->cursor_y = line_num;
    win->cursor_x = 0;
}

void buf_find_matching_bracket(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!buf || !win) return;
    cur_sync_from_window(buf, win);
    if (buf->cursor_y >= buf->num_rows) { cur_sync_to_window(buf, win); return; }

    Row *row = &buf->rows[buf->cursor_y];
    if (buf->cursor_x >= (int)row->chars.len) return;

    char ch = row->chars.data[buf->cursor_x];
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
    int y = buf->cursor_y;
    int x = buf->cursor_x + direction;

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
                    buf->cursor_y = y;
                    buf->cursor_x = x;
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
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!buf || !win || win->cursor_y >= buf->num_rows) return;

    Row *row = &buf->rows[win->cursor_y];

    /* Find word boundaries */
    int start = win->cursor_x;
    int end = win->cursor_x;

    /* Move start to beginning of word */
    while (start > 0 && !isspace(row->chars.data[start - 1])) {
        start--;
    }

    /* Move end to end of word */
    while (end < (int)row->chars.len && !isspace(row->chars.data[end])) {
        end++;
    }

    /* Set visual selection */
    win->visual_start_x = start;
    win->visual_start_y = win->cursor_y;
    win->cursor_x = end;
}

void buf_select_line(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!buf || !win) return;

    win->visual_start_x = 0;
    win->visual_start_y = win->cursor_y;

    if (win->cursor_y < buf->num_rows) {
        win->cursor_x = buf->rows[win->cursor_y].chars.len;
    }
}

void buf_select_all(void) {
    Buffer *buf = buf_cur(); Window *win = window_cur();
    if (!buf || !win) return;

    win->visual_start_x = 0;
    win->visual_start_y = 0;

    win->cursor_y = buf->num_rows - 1;
    if (win->cursor_y < 0) win->cursor_y = 0;

    if (win->cursor_y < buf->num_rows) {
        win->cursor_x = buf->rows[win->cursor_y].chars.len;
    }
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
    int cur_y = buf->cursor_y;
    int cur_x = buf->cursor_x;

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
    if (!buf || sy > ey || (sy == ey && sx > ex)) return;

    Row *start = &buf->rows[sy];
    Row *end   = &buf->rows[ey];

    SizedStr tail = sstr_new();
    if (ex + 1 < (int)end->chars.len) {
        sstr_append(&tail, end->chars.data + ex + 1, end->chars.len - (ex + 1));
    }

    if (sx < 0) sx = 0;
    if (sx > (int)start->chars.len) sx = start->chars.len;
    start->chars.len = sx;
    if (start->chars.data) start->chars.data[start->chars.len] = '\0';
    buf_row_update(start);

    for (int i = sy + 1; i <= ey && sy + 1 < buf->num_rows; i++) {
        buf_row_del(sy + 1);
    }

    if (tail.len > 0) {
        start = &buf->rows[sy];
        sstr_append(&start->chars, tail.data, tail.len);
        buf_row_update(start);
    }
    sstr_free(&tail);

    buf->cursor_y = sy;
    buf->cursor_x = sx;
}

void buf_delete_around_char(void) {
    Buffer *buf = buf_cur();
    if (!buf) return;
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
    bh_delete_range(buf, sy, sx, ey, ex);
    ed_set_status_message("Deleted around %c", (open == close) ? open : close);
}

void buf_delete_inside_char(void) {
    Buffer *buf = buf_cur();
    if (!buf) return;
    ed_set_status_message("di: target?");
    int t = ed_read_key();
    char open = 0, close = 0;
    if (!bh_map_delim(t, &open, &close)) { ed_set_status_message("di: unsupported target"); return; }
    int oy, ox, cy, cx;
    if (!bh_find_enclosing_pair(buf, open, close, &oy, &ox, &cy, &cx)) { ed_set_status_message("di: no enclosing pair"); return; }
    int sy = oy, sx = ox + 1;
    int ey = cy, ex = cx - 1;
    if (ey < sy || (ey == sy && ex < sx)) { ed_set_status_message("di: empty"); return; }
    bh_delete_range(buf, sy, sx, ey, ex);
    ed_set_status_message("Deleted inside %c", (open == close) ? open : close);
}
