#include "textobj.h"
#include "row.h"
#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <string.h>

/* Internal helpers */
static int clamp_line(const Buffer *buf, int line) {
    if (!buf || buf->num_rows <= 0)
        return -1;
    if (line < 0)
        line = 0;
    if (line >= buf->num_rows)
        line = buf->num_rows - 1;
    return line;
}

static int clamp_col(const Row *row, int col) {
    int len = row ? (int)row->chars.len : 0;
    if (col < 0)
        col = 0;
    if (col > len)
        col = len;
    return col;
}

static int is_blank_row(const Row *row) {
    if (!row)
        return 1;
    if (row->chars.len == 0)
        return 1;
    for (size_t i = 0; i < row->chars.len; i++) {
        char c = row->chars.data[i];
        if (!(c == ' ' || c == '\t'))
            return 0;
    }
    return 1;
}

static int set_selection(TextSelection *sel, TextPos start, TextPos end,
                         TextPos cursor) {
    if (!sel)
        return 0;
    /* Ensure start <= end */
    if (start.line > end.line ||
        (start.line == end.line && start.col > end.col)) {
        TextPos tmp = start;
        start = end;
        end = tmp;
    }
    sel->start = start;
    sel->end = end;
    sel->cursor = cursor;
    return 1;
}

/* UTF-8 helpers (byte-oriented, same rules as buf_helpers) */
static int utf8_cp_start(const char *s, int len, int idx) {
    if (idx < 0)
        return 0;
    if (idx > len)
        idx = len;
    while (idx > 0 && ((unsigned char)s[idx] & 0xC0) == 0x80)
        idx--;
    return idx;
}
static int utf8_next_cp(const char *s, int len, int idx) {
    if (idx < 0)
        idx = 0;
    if (idx >= len)
        return len;
    unsigned char c = (unsigned char)s[idx];
    int adv = 1;
    if ((c & 0x80) == 0)
        adv = 1;
    else if ((c & 0xE0) == 0xC0)
        adv = 2;
    else if ((c & 0xF0) == 0xE0)
        adv = 3;
    else if ((c & 0xF8) == 0xF0)
        adv = 4;
    if (idx + adv > len)
        adv = len - idx;
    return idx + adv;
}
static int utf8_prev_cp(const char *s, int len, int idx) {
    if (idx <= 0)
        return -1;
    if (idx > len)
        idx = len;
    idx--;
    while (idx > 0 && ((unsigned char)s[idx] & 0xC0) == 0x80)
        idx--;
    return idx;
}
static int is_word_byte(unsigned char b) {
    return (b == '_' || (b >= '0' && b <= '9') || (b >= 'A' && b <= 'Z') ||
            (b >= 'a' && b <= 'z') || (b & 0x80));
}
static int is_word_cp(const char *s, int len, int idx) {
    if (idx < 0 || idx >= len)
        return 0;
    return is_word_byte((unsigned char)s[idx]);
}

static int column_in_word(const Row *row, int col) {
    if (!row || row->chars.len == 0)
        return 0;
    const char *s = row->chars.data;
    int len = (int)row->chars.len;
    if (col < 0 || col >= len)
        return 0;
    int idx = utf8_cp_start(s, len, col);
    return is_word_cp(s, len, idx);
}

static int word_range_at(const Row *row, int col, int *sx, int *ex) {
    if (!row || row->chars.len == 0 || !sx || !ex)
        return 0;
    const char *s = row->chars.data;
    int len = (int)row->chars.len;
    int cx = col;
    if (cx < 0)
        cx = 0;
    if (cx >= len)
        cx = len - 1;

    int i = utf8_cp_start(s, len, cx);
    if (!is_word_cp(s, len, i)) {
        int j = utf8_prev_cp(s, len, i);
        while (j >= 0 && !is_word_cp(s, len, j)) {
            j = utf8_prev_cp(s, len, j);
        }
        if (j < 0)
            return 0;
        i = j;
    }

    int sx_local = i;
    int prev = utf8_prev_cp(s, len, sx_local);
    while (prev >= 0 && is_word_cp(s, len, prev)) {
        sx_local = prev;
        prev = utf8_prev_cp(s, len, prev);
    }
    int ex_local = utf8_next_cp(s, len, i);
    while (ex_local < len && is_word_cp(s, len, ex_local)) {
        ex_local = utf8_next_cp(s, len, ex_local);
    }

    if (sx_local >= ex_local)
        return 0;
    *sx = sx_local;
    *ex = ex_local;
    return 1;
}

static int word_start_at_or_after(const Row *row, int col) {
    if (!row || row->chars.len == 0)
        return -1;
    const char *s = row->chars.data;
    int len = (int)row->chars.len;
    int idx = col;
    if (idx < 0)
        idx = 0;
    if (idx >= len)
        return -1;
    idx = utf8_cp_start(s, len, idx);
    if (!is_word_cp(s, len, idx)) {
        idx = utf8_next_cp(s, len, idx);
        while (idx < len && !is_word_cp(s, len, idx)) {
            idx = utf8_next_cp(s, len, idx);
        }
        if (idx >= len)
            return -1;
    }
    while (1) {
        int prev = utf8_prev_cp(s, len, idx);
        if (prev < 0 || !is_word_cp(s, len, prev))
            break;
        idx = prev;
    }
    return idx;
}

static int word_start_before(const Row *row, int col) {
    if (!row || row->chars.len == 0)
        return -1;
    const char *s = row->chars.data;
    int len = (int)row->chars.len;
    int idx = utf8_prev_cp(s, len, col);
    while (idx >= 0 && !is_word_cp(s, len, idx)) {
        idx = utf8_prev_cp(s, len, idx);
    }
    if (idx < 0)
        return -1;
    while (1) {
        int prev = utf8_prev_cp(s, len, idx);
        if (prev < 0 || !is_word_cp(s, len, prev))
            break;
        idx = prev;
    }
    return idx;
}

static int find_next_word(Buffer *buf, int line, int col, int *out_line,
                          int *out_sx, int *out_ex) {
    if (!buf || buf->num_rows <= 0 || !out_sx || !out_ex)
        return 0;
    int start_line = clamp_line(buf, line);
    if (start_line < 0)
        return 0;
    for (int y = start_line; y < buf->num_rows; y++) {
        Row *row = &buf->rows[y];
        int start_col = (y == start_line) ? col : 0;
        if (start_col > (int)row->chars.len)
            start_col = (int)row->chars.len;
        int word_col = word_start_at_or_after(row, start_col);
        if (word_col < 0)
            continue;
        if (!word_range_at(row, word_col, out_sx, out_ex))
            continue;
        if (out_line)
            *out_line = y;
        return 1;
    }
    return 0;
}

static int find_prev_word(Buffer *buf, int line, int col, int *out_line,
                          int *out_sx, int *out_ex) {
    if (!buf || buf->num_rows <= 0 || !out_sx || !out_ex)
        return 0;
    int start_line = clamp_line(buf, line);
    if (start_line < 0)
        return 0;
    for (int y = start_line; y >= 0; y--) {
        Row *row = &buf->rows[y];
        int start_col = (y == start_line) ? col : (int)row->chars.len;
        if (start_col > (int)row->chars.len)
            start_col = (int)row->chars.len;
        int word_col = word_start_before(row, start_col);
        if (word_col < 0)
            continue;
        if (!word_range_at(row, word_col, out_sx, out_ex))
            continue;
        if (out_line)
            *out_line = y;
        return 1;
    }
    return 0;
}

static int paragraph_range(Buffer *buf, int line, int *out_sy, int *out_ey) {
    if (!buf || buf->num_rows == 0 || !out_sy || !out_ey)
        return 0;
    int y = clamp_line(buf, line);
    if (y < 0)
        return 0;

    int sy = y;
    while (sy > 0) {
        const Row *prev = &buf->rows[sy - 1];
        if (is_blank_row(prev))
            break;
        sy--;
    }

    int ey = y;
    while (ey + 1 < buf->num_rows) {
        const Row *next = &buf->rows[ey + 1];
        if (is_blank_row(next))
            break;
        ey++;
    }

    *out_sy = sy;
    *out_ey = ey;
    return 1;
}

/* Bracket helpers */
static int map_delim(char t, char *open, char *close) {
    if (!open || !close)
        return 0;
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

static int is_unescaped_quote(const Row *row, int x) {
    if (!row || x < 0 || x >= (int)row->chars.len)
        return 0;
    int backslashes = 0;
    for (int i = x - 1; i >= 0 && row->chars.data[i] == '\\'; i--)
        backslashes++;
    return (backslashes % 2) == 0;
}

static int find_enclosing_pair(Buffer *buf, int line, int col, char open,
                               char close, int *oy, int *ox, int *cy, int *cx) {
    if (!buf || buf->num_rows == 0 || !oy || !ox || !cy || !cx)
        return 0;
    int cur_y = clamp_line(buf, line);
    if (cur_y < 0)
        return 0;
    const Row *cur_row = &buf->rows[cur_y];
    if (cur_row->chars.len == 0)
        return 0;
    int cur_x = clamp_col(cur_row, col);
    if (cur_x >= (int)cur_row->chars.len && cur_row->chars.len > 0) {
        cur_x = (int)cur_row->chars.len - 1;
    }

    int by = -1, bx = -1, found_open = 0;
    if (open == close) {
        for (int y = cur_y; y >= 0 && !found_open; y--) {
            const Row *row = &buf->rows[y];
            int startx = (y == cur_y) ? cur_x : (int)row->chars.len - 1;
            if (startx >= (int)row->chars.len)
                startx = (int)row->chars.len - 1;
            for (int x = startx; x >= 0; x--) {
                if (row->chars.data[x] == open && is_unescaped_quote(row, x)) {
                    by = y;
                    bx = x;
                    found_open = 1;
                    break;
                }
            }
        }
    } else {
        int depth = 0;
        for (int y = cur_y; y >= 0 && !found_open; y--) {
            const Row *row = &buf->rows[y];
            int startx = (y == cur_y) ? cur_x : (int)row->chars.len - 1;
            if (startx >= (int)row->chars.len)
                startx = (int)row->chars.len - 1;
            for (int x = startx; x >= 0; x--) {
                char c = row->chars.data[x];
                if (c == close)
                    depth++;
                else if (c == open) {
                    if (depth == 0) {
                        by = y;
                        bx = x;
                        found_open = 1;
                        break;
                    } else {
                        depth--;
                        if (depth == 0) {
                            by = y;
                            bx = x;
                            found_open = 1;
                            break;
                        }
                    }
                }
            }
        }
    }
    if (!found_open)
        return 0;

    int fy = -1, fx = -1, found_close = 0;
    if (open == close) {
        for (int y = by; y < buf->num_rows && !found_close; y++) {
            const Row *row = &buf->rows[y];
            int startx = (y == by) ? (bx + 1) : 0;
            for (int x = startx; x < (int)row->chars.len; x++) {
                if (row->chars.data[x] == close && is_unescaped_quote(row, x)) {
                    fy = y;
                    fx = x;
                    found_close = 1;
                    break;
                }
            }
        }
    } else {
        int depth = 0;
        for (int y = by; y < buf->num_rows && !found_close; y++) {
            const Row *row = &buf->rows[y];
            int startx = (y == by) ? (bx + 1) : 0;
            for (int x = startx; x < (int)row->chars.len; x++) {
                char c = row->chars.data[x];
                if (c == open)
                    depth++;
                else if (c == close) {
                    if (depth == 0) {
                        fy = y;
                        fx = x;
                        found_close = 1;
                        break;
                    } else
                        depth--;
                }
            }
        }
    }
    if (!found_close)
        return 0;

    *oy = by;
    *ox = bx;
    *cy = fy;
    *cx = fx;
    return 1;
}

static int brackets_select(Buffer *buf, int line, int col, char open,
                           char close, int include_delims, TextSelection *sel) {
    int oy, ox, cy, cx;
    if (!find_enclosing_pair(buf, line, col, open, close, &oy, &ox, &cy, &cx))
        return 0;

    if (include_delims) {
        TextPos start = {oy, ox};
        TextPos end = {cy, cx + 1};
        TextPos cursor = {clamp_line(buf, line),
                          clamp_col(&buf->rows[clamp_line(buf, line)], col)};
        return set_selection(sel, start, end, cursor);
    }

    TextPos start = {oy, ox + 1};
    TextPos end = {cy, cx};
    TextPos cursor = {clamp_line(buf, line),
                      clamp_col(&buf->rows[clamp_line(buf, line)], col)};
    return set_selection(sel, start, end, cursor);
}

/* Public API implementations */

int textobj_word(Buffer *buf, int line, int col, TextSelection *sel) {
    int y = clamp_line(buf, line);
    if (y < 0)
        return 0;
    Row *row = &buf->rows[y];
    int x = clamp_col(row, col);
    int sx = 0, ex = 0;
    if (!word_range_at(row, x, &sx, &ex))
        return 0;
    TextPos cursor = {y, x};
    return set_selection(sel, (TextPos){y, sx}, (TextPos){y, ex}, cursor);
}

int textobj_line(Buffer *buf, int line, int col, TextSelection *sel) {
    int y = clamp_line(buf, line);
    if (y < 0)
        return 0;
    Row *row = &buf->rows[y];
    int x = clamp_col(row, col);
    int len = (int)row->chars.len;
    TextPos cursor = {y, x};
    return set_selection(sel, (TextPos){y, 0}, (TextPos){y, len}, cursor);
}

int textobj_brackets(Buffer *buf, int line, int col, TextSelection *sel) {
    int y = clamp_line(buf, line);
    if (y < 0)
        return 0;
    Row *row = &buf->rows[y];
    if (!row || row->chars.len == 0)
        return 0;
    int x = clamp_col(row, col);
    int probe = x;
    if (probe >= (int)row->chars.len)
        probe = (int)row->chars.len - 1;

    char open = 0, close = 0;
    if (map_delim(row->chars.data[probe], &open, &close) ||
        (probe > 0 && map_delim(row->chars.data[probe - 1], &open, &close))) {
        return brackets_select(buf, y, x, open, close, 0, sel);
    }

    static const char pairs[][2] = {{'(', ')'}, {'[', ']'}, {'{', '}'},
                                    {'<', '>'}, {'"', '"'}, {'\'', '\''},
                                    {'`', '`'}};
    int best_oy = -1, best_ox = -1, best_cy = INT_MAX, best_cx = INT_MAX;
    char best_open = 0, best_close = 0;
    bool found = false;
    for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
        int oy = 0, ox = 0, cy = 0, cx = 0;
        if (!find_enclosing_pair(buf, y, x, pairs[i][0], pairs[i][1], &oy, &ox,
                                 &cy, &cx))
            continue;
        if (!found || oy > best_oy || (oy == best_oy && ox > best_ox) ||
            (oy == best_oy && ox == best_ox &&
             (cy < best_cy || (cy == best_cy && cx < best_cx)))) {
            best_open = pairs[i][0];
            best_close = pairs[i][1];
            best_oy = oy;
            best_ox = ox;
            best_cy = cy;
            best_cx = cx;
            found = true;
        }
    }

    if (!found)
        return 0;
    return brackets_select(buf, y, x, best_open, best_close, 0, sel);
}

int textobj_brackets_with(Buffer *buf, int line, int col, char open, char close,
                          bool include_delims, TextSelection *sel) {
    if (!open || !close)
        return 0;
    return brackets_select(buf, line, col, open, close, include_delims, sel);
}

int textobj_to_word_end(Buffer *buf, int line, int col, TextSelection *sel) {
    int y = clamp_line(buf, line);
    if (y < 0)
        return 0;
    Row *row = &buf->rows[y];
    int x = clamp_col(row, col);
    int sx = 0, ex = 0;
    int target_line = y;
    int start_col = x;

    if (column_in_word(row, x)) {
        if (!word_range_at(row, x, &sx, &ex))
            return 0;
        if (x >= ex - 1) {
            if (!find_next_word(buf, y, ex, &target_line, &sx, &ex))
                return 0;
            row = &buf->rows[target_line];
            start_col = sx;
        } else {
            if (start_col < sx)
                start_col = sx;
            if (start_col > ex)
                start_col = ex;
        }
    } else {
        if (!find_next_word(buf, y, x, &target_line, &sx, &ex))
            return 0;
        row = &buf->rows[target_line];
        start_col = sx;
    }

    int cursor_col =
        utf8_prev_cp(row->chars.data, (int)row->chars.len, ex);
    if (cursor_col < 0)
        cursor_col = start_col;
    TextPos cursor = {target_line, cursor_col};
    return set_selection(sel, (TextPos){target_line, start_col},
                         (TextPos){target_line, ex}, cursor);
}

int textobj_to_word_start(Buffer *buf, int line, int col, TextSelection *sel) {
    int y = clamp_line(buf, line);
    if (y < 0)
        return 0;
    Row *row = &buf->rows[y];
    int orig_x = clamp_col(row, col);
    int sx = 0, ex = 0;
    int target_line = y;

    if (column_in_word(row, orig_x)) {
        if (!word_range_at(row, orig_x, &sx, &ex))
            return 0;
        if (orig_x == sx) {
            if (!find_prev_word(buf, y, sx, &target_line, &sx, &ex))
                return 0;
            row = &buf->rows[target_line];
        }
    } else {
        if (!find_prev_word(buf, y, orig_x, &target_line, &sx, &ex))
            return 0;
        row = &buf->rows[target_line];
    }

    int row_len = (int)row->chars.len;
    int end_col = ex;
    if (target_line == y) {
        if (orig_x >= sx && orig_x < ex) {
            end_col = utf8_next_cp(row->chars.data, row_len, orig_x);
            if (end_col > ex)
                end_col = ex;
        } else {
            end_col = orig_x;
            if (end_col < sx)
                end_col = sx;
            if (end_col > row_len)
                end_col = row_len;
        }
    }

    TextPos cursor = {target_line, sx};
    return set_selection(sel, (TextPos){target_line, sx},
                         (TextPos){target_line, end_col}, cursor);
}

int textobj_to_line_end(Buffer *buf, int line, int col, TextSelection *sel) {
    int y = clamp_line(buf, line);
    if (y < 0)
        return 0;
    Row *row = &buf->rows[y];
    int x = clamp_col(row, col);
    int len = (int)row->chars.len;
    if (x > len)
        x = len;
    int cursor_col = (len > 0) ? (len - 1) : 0;
    TextPos cursor = {y, cursor_col};
    return set_selection(sel, (TextPos){y, x}, (TextPos){y, len}, cursor);
}

int textobj_to_line_start(Buffer *buf, int line, int col, TextSelection *sel) {
    int y = clamp_line(buf, line);
    if (y < 0)
        return 0;
    Row *row = &buf->rows[y];
    int x = clamp_col(row, col);
    int end_col = x;
    if (end_col < (int)row->chars.len)
        end_col++;
    TextPos cursor = {y, 0};
    return set_selection(sel, (TextPos){y, 0}, (TextPos){y, end_col}, cursor);
}

int textobj_to_file_end(Buffer *buf, int line, int col, TextSelection *sel) {
    if (!buf || buf->num_rows <= 0)
        return 0;
    int y = clamp_line(buf, line);
    if (y < 0)
        return 0;
    Row *row = &buf->rows[y];
    int x = clamp_col(row, col);
    int last_y = buf->num_rows - 1;
    Row *last = &buf->rows[last_y];
    int last_len = (int)last->chars.len;
    int cursor_col = (last_len > 0) ? (last_len - 1) : 0;
    TextPos cursor = {last_y, cursor_col};
    return set_selection(sel, (TextPos){y, x}, (TextPos){last_y, last_len},
                         cursor);
}

int textobj_to_file_start(Buffer *buf, int line, int col, TextSelection *sel) {
    if (!buf || buf->num_rows <= 0)
        return 0;
    int y = clamp_line(buf, line);
    if (y < 0)
        return 0;
    Row *row = &buf->rows[y];
    int x = clamp_col(row, col);
    int end_col = x;
    if (end_col < (int)row->chars.len)
        end_col++;
    TextPos cursor = {0, 0};
    return set_selection(sel, (TextPos){0, 0}, (TextPos){y, end_col}, cursor);
}

int textobj_to_paragraph_end(Buffer *buf, int line, int col,
                             TextSelection *sel) {
    int y = clamp_line(buf, line);
    if (y < 0)
        return 0;
    int sy = 0, ey = 0;
    if (!paragraph_range(buf, y, &sy, &ey))
        return 0;
    Row *row = &buf->rows[y];
    int x = clamp_col(row, col);
    Row *end_row = &buf->rows[ey];
    int end_line = ey;
    int end_col = (int)end_row->chars.len;
    int cursor_col = (end_col > 0) ? (end_col - 1) : 0;
    if (ey + 1 < buf->num_rows && is_blank_row(&buf->rows[ey + 1])) {
        end_line = ey + 1;
        end_col = 0;
        cursor_col = (int)end_row->chars.len;
    }
    TextPos cursor = {ey, cursor_col};
    return set_selection(sel, (TextPos){y, x}, (TextPos){end_line, end_col},
                         cursor);
}

int textobj_to_paragraph_start(Buffer *buf, int line, int col,
                               TextSelection *sel) {
    int y = clamp_line(buf, line);
    if (y < 0)
        return 0;
    int sy = 0, ey = 0;
    if (!paragraph_range(buf, y, &sy, &ey))
        return 0;
    Row *row = &buf->rows[y];
    int x = clamp_col(row, col);
    (void)ey; /* ey unused but computed for symmetry */
    int end_col = x;
    if (end_col < (int)row->chars.len)
        end_col++;
    TextPos cursor = {sy, 0};
    return set_selection(sel, (TextPos){sy, 0}, (TextPos){y, end_col}, cursor);
}

int textobj_paragraph(Buffer *buf, int line, int col, TextSelection *sel) {
    int y = clamp_line(buf, line);
    if (y < 0)
        return 0;
    int sy = 0, ey = 0;
    if (!paragraph_range(buf, y, &sy, &ey))
        return 0;
    (void)col;
    Row *end_row = &buf->rows[ey];
    int end_line = ey;
    int end_col = (int)end_row->chars.len;
    int cursor_col = (end_col > 0) ? (end_col - 1) : 0;
    if (ey + 1 < buf->num_rows && is_blank_row(&buf->rows[ey + 1])) {
        end_line = ey + 1;
        end_col = 0;
        cursor_col = (int)end_row->chars.len;
    }
    TextPos cursor = {ey, cursor_col};
    return set_selection(sel, (TextPos){sy, 0}, (TextPos){end_line, end_col},
                         cursor);
}

/*
 * textobj_char_at_cursor - Select single character at cursor (for 'x' command)
 *
 * Selects the character under the cursor, handling UTF-8 multibyte characters
 * correctly. Returns 0 if cursor is at end of line (nothing to delete).
 */
int textobj_char_at_cursor(Buffer *buf, int line, int col, TextSelection *sel) {
    int y = clamp_line(buf, line);
    if (y < 0)
        return 0;
    Row *row = &buf->rows[y];
    int x = clamp_col(row, col);

    /* Nothing to delete if at or past end of line */
    if (x >= (int)row->chars.len)
        return 0;

    /* Handle UTF-8: select full codepoint */
    const char *s = row->chars.data;
    int len = (int)row->chars.len;
    int end_x = utf8_next_cp(s, len, x);

    /* Cursor stays at current position */
    TextPos cursor = {y, x};
    return set_selection(sel, (TextPos){y, x}, (TextPos){y, end_x}, cursor);
}

/*
 * textobj_line_with_newline - Select entire line including newline (for 'dd'
 * command)
 *
 * Returns a selection that signals deletion of the entire row from the buffer.
 * Uses the special pattern: start=(y,0), end=(y+1,0) to indicate whole-line
 * deletion. For the last line, selects just the line content without newline.
 */
int textobj_line_with_newline(Buffer *buf, int line, int col,
                               TextSelection *sel) {
    int y = clamp_line(buf, line);
    if (y < 0)
        return 0;
    Row *row = &buf->rows[y];
    int x = clamp_col(row, col);

    TextPos cursor = {y, x};

    if (y + 1 < buf->num_rows) {
        /* Not the last line - delete includes newline
         * Signal whole-line deletion with end=(y+1, 0) */
        return set_selection(sel, (TextPos){y, 0}, (TextPos){y + 1, 0}, cursor);
    } else {
        /* Last line - delete contents only (no newline to delete) */
        cursor = (TextPos){y, 0};
        return set_selection(sel, (TextPos){y, 0},
                             (TextPos){y, (int)row->chars.len}, cursor);
    }
}
