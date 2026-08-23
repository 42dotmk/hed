#include "utils/under_cursor.h"
#include "buf/buf_helpers.h"
#include "editor.h"
#include "lib/errors.h"
#include "lib/safe_string.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* Borrow the word under the cursor as a view into the row's own buffer —
 * no allocation. The view is valid only until that buffer is next edited,
 * so consume it before mutating. Callers that need to keep the bytes
 * should copy via strbuf_from_view(). */
int buf_word_view_under_cursor(StrView *out) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win) || !PTR_VALID(out))
        return 0;
    if (!BOUNDS_CHECK(win->cursor.y, buf->num_rows))
        return 0;
    /* Vim <cword>: if the cursor sits on whitespace, use the next word on
     * the line (textobj_word would yield the blank run itself). */
    Row *cur_row = &buf->rows[win->cursor.y];
    int cx = win->cursor.x;
    if (cx >= (int)cur_row->chars.len)
        cx = (int)cur_row->chars.len - 1;
    while (cx >= 0 && cx < (int)cur_row->chars.len &&
           (cur_row->chars.data[cx] == ' ' || cur_row->chars.data[cx] == '\t'))
        cx++;
    if (cx < 0 || cx >= (int)cur_row->chars.len)
        return 0;
    TextSelection sel;
    if (!textobj_word(buf, win->cursor.y, cx, &sel))
        return 0;
    Row *row = &buf->rows[sel.start.line];
    *out = strview(row->chars.data + sel.start.col,
                   (size_t)(sel.end.col - sel.start.col));
    return 1;
}

int buf_get_word_under_cursor(StrBuf *out) {
    if (!PTR_VALID(out))
        return 0;
    StrView v;
    if (!buf_word_view_under_cursor(&v))
        return 0;
    strbuf_free(out);
    *out = strbuf_from_view(v);
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

static void strip_path_position(StrBuf *path, int *out_line, int *out_col) {
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

int buf_get_path_under_cursor(StrBuf *out, int *out_line, int *out_col) {
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

    strbuf_free(out);
    *out = strbuf_from(s + start, (size_t)(end - start));
    if (!out->data || out->len == 0) {
        strbuf_free(out);
        return 0;
    }

    /* URI-shaped tokens (mail://thread:…, http://…) stay verbatim —
     * their trailing :digits belong to the target, not a position. */
    if (!strstr(out->data, "://"))
        strip_path_position(out, out_line, out_col);
    if (!out->data || out->len == 0) {
        strbuf_free(out);
        return 0;
    }

    return 1;
}

int buf_get_paragraph_under_cursor(StrBuf *out) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win) || !PTR_VALID(out))
        return 0;
    TextSelection sel;
    if (!textobj_paragraph(buf, win->cursor.y, win->cursor.x, &sel))
        return 0;
    strbuf_free(out);
    *out = strbuf_new();
    for (int y = sel.start.line; y <= sel.end.line; y++) {
        Row *r = &buf->rows[y];
        int start_col = (y == sel.start.line) ? sel.start.col : 0;
        int end_col = (y == sel.end.line) ? sel.end.col : (int)r->chars.len;
        if (start_col < 0)
            start_col = 0;
        if (end_col > (int)r->chars.len)
            end_col = (int)r->chars.len;
        if (end_col > start_col) {
            strbuf_append(out, r->chars.data + start_col,
                          (size_t)(end_col - start_col));
        }
        if (y != sel.end.line)
            strbuf_append_char(out, '\n');
    }
    return 1;
}
