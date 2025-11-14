#include "../hed.h"

/*
 * Visual Mode Implementation - Complete Rewrite
 * ==============================================
 *
 * Clean character-wise visual selection with proper operations.
 */

/* Helper: delete range of characters from SizedStr */
static void sstr_delete_range(SizedStr *s, size_t pos, size_t len) {
    if (!s || pos >= s->len || len == 0) return;
    if (pos + len > s->len) len = s->len - pos;

    /* Shift remaining data left */
    memmove(s->data + pos, s->data + pos + len, s->len - pos - len);
    s->len -= len;
    s->data[s->len] = '\0';
}

/* Get normalized selection range (start <= end) */
int visual_get_range(int *start_y, int *start_x, int *end_y, int *end_x) {
    Window *win = window_cur();
    Buffer *buf = buf_cur();
    if (!PTR_VALID(win) || !PTR_VALID(buf)) return 0;

    int ay = win->visual_start_y;
    int ax = win->visual_start_x;
    int by = win->cursor_y;
    int bx = win->cursor_x;

    /* Normalize: ensure start <= end */
    if (ay < by || (ay == by && ax <= bx)) {
        /* Anchor is before cursor */
        if (start_y) *start_y = ay;
        if (start_x) *start_x = ax;
        if (end_y) *end_y = by;
        if (end_x) *end_x = bx;
    } else {
        /* Cursor is before anchor */
        if (start_y) *start_y = by;
        if (start_x) *start_x = bx;
        if (end_y) *end_y = ay;
        if (end_x) *end_x = ax;
    }

    return 1;
}

/* Yank (copy) selection to clipboard */
void visual_yank(void) {
    Buffer *buf = buf_cur();
    if (!PTR_VALID(buf)) return;

    int sy, sx, ey, ex;
    if (!visual_get_range(&sy, &sx, &ey, &ex)) return;

    /* Build yanked content */
    sstr_free(&E.clipboard);
    E.clipboard = sstr_new();

    if (sy == ey) {
        /* Single line selection */
        if (BOUNDS_CHECK(sy, buf->num_rows)) {
            Row *row = &buf->rows[sy];
            int len = ex - sx + 1;  /* Inclusive */
            if (sx < (int)row->chars.len) {
                if (ex >= (int)row->chars.len) len = (int)row->chars.len - sx;
                if (len > 0) {
                    sstr_append(&E.clipboard, row->chars.data + sx, len);
                }
            }
        }
    } else {
        /* Multi-line selection */
        for (int y = sy; y <= ey && y < buf->num_rows; y++) {
            Row *row = &buf->rows[y];
            if (y == sy) {
                /* First line: from sx to end */
                if (sx < (int)row->chars.len) {
                    sstr_append(&E.clipboard, row->chars.data + sx, row->chars.len - sx);
                }
                sstr_append_char(&E.clipboard, '\n');
            } else if (y == ey) {
                /* Last line: from start to ex */
                int len = ex + 1;  /* Inclusive */
                if (len > (int)row->chars.len) len = (int)row->chars.len;
                if (len > 0) {
                    sstr_append(&E.clipboard, row->chars.data, len);
                }
            } else {
                /* Middle lines: entire line */
                sstr_append(&E.clipboard, row->chars.data, row->chars.len);
                sstr_append_char(&E.clipboard, '\n');
            }
        }
    }

    /* Update registers */
    regs_set_yank(E.clipboard.data, E.clipboard.len);

    ed_set_status_message("Yanked %zu chars", E.clipboard.len);
    ed_set_mode(MODE_NORMAL);
}

/* Delete selection */
void visual_delete(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win)) return;

    int sy, sx, ey, ex;
    if (!visual_get_range(&sy, &sx, &ey, &ex)) return;

    /* First yank to clipboard */
    visual_yank();

    /* Now delete */
    if (sy == ey) {
        /* Single line deletion */
        if (BOUNDS_CHECK(sy, buf->num_rows)) {
            Row *row = &buf->rows[sy];
            int len = ex - sx + 1;  /* Inclusive */
            if (sx < (int)row->chars.len) {
                if (ex >= (int)row->chars.len) len = (int)row->chars.len - sx;
                if (len > 0) {
                    sstr_delete_range(&row->chars, sx, len);
                    buf_row_update(row);
                    buf->dirty++;
                }
            }
            /* Position cursor at deletion start */
            win->cursor_y = sy;
            win->cursor_x = sx;
        }
    } else {
        /* Multi-line deletion */
        /* Save content before deletion point on first line */
        SizedStr prefix = sstr_new();
        if (BOUNDS_CHECK(sy, buf->num_rows)) {
            Row *first = &buf->rows[sy];
            if (sx < (int)first->chars.len) {
                prefix = sstr_from(first->chars.data, sx);
            }
        }

        /* Save content after deletion point on last line */
        SizedStr suffix = sstr_new();
        if (BOUNDS_CHECK(ey, buf->num_rows)) {
            Row *last = &buf->rows[ey];
            if (ex + 1 < (int)last->chars.len) {
                suffix = sstr_from(last->chars.data + ex + 1, last->chars.len - ex - 1);
            }
        }

        /* Delete all lines from sy to ey */
        for (int i = 0; i < (ey - sy + 1); i++) {
            if (sy < buf->num_rows) {
                buf_row_del_in(buf, sy);
            }
        }

        /* Insert combined line */
        SizedStr combined = sstr_new();
        sstr_append(&combined, prefix.data, prefix.len);
        sstr_append(&combined, suffix.data, suffix.len);
        buf_row_insert_in(buf, sy, combined.data, combined.len);

        sstr_free(&prefix);
        sstr_free(&suffix);
        sstr_free(&combined);

        /* Position cursor at deletion start */
        win->cursor_y = sy;
        win->cursor_x = sx;
    }

    ed_set_mode(MODE_NORMAL);
    ed_set_status_message("Deleted");
}

/* Change selection (delete + insert mode) */
void visual_change(void) {
    visual_delete();
    ed_set_mode(MODE_INSERT);
    ed_set_status_message("-- INSERT --");
}

/* Indent selected lines */
void visual_indent(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win)) return;

    int sy, sx, ey, ex;
    if (!visual_get_range(&sy, &sx, &ey, &ex)) return;
    (void)sx; (void)ex;  /* Not used for line operations */

    /* Indent each line */
    for (int y = sy; y <= ey && y < buf->num_rows; y++) {
        Row *row = &buf->rows[y];
        /* Insert TAB_STOP spaces at beginning */
        for (int i = 0; i < TAB_STOP; i++) {
            sstr_insert_char(&row->chars, 0, ' ');
        }
        buf_row_update(row);
    }

    buf->dirty++;
    ed_set_mode(MODE_NORMAL);
    ed_set_status_message("Indented %d lines", ey - sy + 1);
}

/* Unindent selected lines */
void visual_unindent(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win)) return;

    int sy, sx, ey, ex;
    if (!visual_get_range(&sy, &sx, &ey, &ex)) return;
    (void)sx; (void)ex;  /* Not used for line operations */

    /* Unindent each line */
    for (int y = sy; y <= ey && y < buf->num_rows; y++) {
        Row *row = &buf->rows[y];
        /* Remove up to TAB_STOP leading spaces */
        int removed = 0;
        while (removed < TAB_STOP && row->chars.len > 0 && row->chars.data[0] == ' ') {
            sstr_delete_char(&row->chars, 0);
            removed++;
        }
        if (removed > 0) buf_row_update(row);
    }

    buf->dirty++;
    ed_set_mode(MODE_NORMAL);
    ed_set_status_message("Unindented %d lines", ey - sy + 1);
}

/* Toggle case of selection */
void visual_toggle_case(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win)) return;

    int sy, sx, ey, ex;
    if (!visual_get_range(&sy, &sx, &ey, &ex)) return;

    /* Toggle case in range */
    for (int y = sy; y <= ey && y < buf->num_rows; y++) {
        Row *row = &buf->rows[y];
        int start_x = (y == sy) ? sx : 0;
        int end_x = (y == ey) ? ex : (int)row->chars.len - 1;

        for (int x = start_x; x <= end_x && x < (int)row->chars.len; x++) {
            char c = row->chars.data[x];
            if (c >= 'a' && c <= 'z') {
                row->chars.data[x] = c - 32;  /* to uppercase */
            } else if (c >= 'A' && c <= 'Z') {
                row->chars.data[x] = c + 32;  /* to lowercase */
            }
        }
        buf_row_update(row);
    }

    buf->dirty++;
    ed_set_mode(MODE_NORMAL);
    ed_set_status_message("Toggled case");
}

/* Convert selection to lowercase */
void visual_lowercase(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win)) return;

    int sy, sx, ey, ex;
    if (!visual_get_range(&sy, &sx, &ey, &ex)) return;

    for (int y = sy; y <= ey && y < buf->num_rows; y++) {
        Row *row = &buf->rows[y];
        int start_x = (y == sy) ? sx : 0;
        int end_x = (y == ey) ? ex : (int)row->chars.len - 1;

        for (int x = start_x; x <= end_x && x < (int)row->chars.len; x++) {
            char c = row->chars.data[x];
            if (c >= 'A' && c <= 'Z') {
                row->chars.data[x] = c + 32;
            }
        }
        buf_row_update(row);
    }

    buf->dirty++;
    ed_set_mode(MODE_NORMAL);
    ed_set_status_message("Lowercased");
}

/* Convert selection to uppercase */
void visual_uppercase(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win)) return;

    int sy, sx, ey, ex;
    if (!visual_get_range(&sy, &sx, &ey, &ex)) return;

    for (int y = sy; y <= ey && y < buf->num_rows; y++) {
        Row *row = &buf->rows[y];
        int start_x = (y == sy) ? sx : 0;
        int end_x = (y == ey) ? ex : (int)row->chars.len - 1;

        for (int x = start_x; x <= end_x && x < (int)row->chars.len; x++) {
            char c = row->chars.data[x];
            if (c >= 'a' && c <= 'z') {
                row->chars.data[x] = c - 32;
            }
        }
        buf_row_update(row);
    }

    buf->dirty++;
    ed_set_mode(MODE_NORMAL);
    ed_set_status_message("Uppercased");
}

/* Visual mode keypress handler */
void visual_mode_keypress(int c) {
    /* Try keybindings first */
    if (keybind_process(c, E.mode)) {
        return;
    }

    /* Handle keys not bound via keybinding system */
    switch (c) {
        case '\x1b':  /* Escape */
            ed_set_mode(MODE_NORMAL);
            ed_set_status_message("");
            break;

        /* Movement keys - extend selection */
        case 'h': case 'j': case 'k': case 'l':
        case 'w': case 'b': case 'e':
        case '0': case '$':
        case 'G': case 'g':
        case KEY_ARROW_UP: case KEY_ARROW_DOWN:
        case KEY_ARROW_RIGHT: case KEY_ARROW_LEFT:
        case KEY_HOME: case KEY_END:
        case KEY_PAGE_UP: case KEY_PAGE_DOWN:
            ed_move_cursor(c);
            break;

        /* Operations (many handled by keybindings, these are fallbacks) */
        case 'y':
            visual_yank();
            break;
        case 'd':
        case 'x':
            visual_delete();
            break;
        case 'c':
            visual_change();
            break;
        case '>':
            visual_indent();
            break;
        case '<':
            visual_unindent();
            break;
        case '~':
            visual_toggle_case();
            break;
        case 'u':
            visual_lowercase();
            break;
        case 'U':
            visual_uppercase();
            break;

        default:
            /* Ignore unknown keys */
            break;
    }
}
