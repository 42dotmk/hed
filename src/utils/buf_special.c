#include "utils/buf_special.h"

#include "buf/buf_helpers.h"
#include "buf/row.h"
#include "editor.h"
#include "lib/strutil.h"
#include "stb_ds.h"
#include "ui/winmodal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int buf_special_find(const char *name) {
    if (!name)
        return -1;
    int idx = buf_find_by_filename(name);
    if (idx >= 0)
        return idx;
    for (int i = 0; i < (int)arrlen(E.buffers); i++) {
        const char *t = E.buffers[i].title;
        if (t && strcmp(t, name) == 0)
            return i;
    }
    return -1;
}

int buf_special_get(const BufSpecial *spec, int *created) {
    if (created)
        *created = 0;
    if (!spec)
        return -1;

    int idx = buf_special_find(spec->name);
    if (idx < 0) {
        EdError e =
            spec->as_filename
                ? buf_new(spec->name, &idx)
                : buf_new_scratch(spec->title ? spec->title : spec->name, &idx);
        if (e != ED_OK)
            return -1;
        if (created)
            *created = 1;
    }

    Buffer *b = &E.buffers[idx];
    const char *title = spec->title ? spec->title : spec->name;
    if (title) {
        free(b->title);
        b->title = strdup(title);
    }
    if (spec->filetype) {
        free(b->filetype);
        b->filetype = strdup(spec->filetype);
    }
    b->readonly = spec->readonly;
    return idx;
}

void buf_special_clear(Buffer *b) {
    if (!b)
        return;
    for (int i = 0; i < b->num_rows; i++)
        row_free(&b->rows[i]);
    free(b->rows);
    b->rows = NULL;
    b->num_rows = 0;
    if (b->cursor) {
        b->cursor->x = 0;
        b->cursor->y = 0;
    }
}

void buf_special_add(Buffer *b, const char *line, size_t len) {
    if (b && line)
        buf_row_insert_in(b, b->num_rows, line, len);
}

void buf_special_addf(Buffer *b, const char *fmt, ...) {
    char line[4096]; /* dired plan rows carry full paths */
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    buf_special_add(b, line, strnlen(line, sizeof(line)));
}

void buf_special_add_lines(Buffer *b, char **lines, int n) {
    for (int i = 0; i < n; i++)
        if (lines && lines[i])
            buf_special_add(b, lines[i], strlen(lines[i]));
}

static int valid_idx(int idx) {
    return idx >= 0 && idx < (int)arrlen(E.buffers);
}

int buf_special_show(int idx) {
    if (!valid_idx(idx))
        return -1;
    Buffer *b = &E.buffers[idx];
    b->dirty = 0;
    Window *win = window_cur();
    if (win) {
        win_attach_buf(win, b);
        win->cursor.x = 0;
        win->cursor.y = 0;
    }
    E.current_buffer = idx;
    return 0;
}

int buf_special_show_split(int idx, int vertical) {
    if (!valid_idx(idx))
        return -1;
    E.buffers[idx].dirty = 0;
    for (int i = 0; i < (int)arrlen(E.windows); i++) {
        Window *w = &E.windows[i];
        if (w->visible && !w->is_modal && w->buffer_index == idx) {
            if (i != E.current_window) {
                E.windows[E.current_window].focus = 0;
                w->focus = 1;
                E.current_window = i;
                E.current_buffer = idx;
            }
            return 0;
        }
    }
    if (vertical)
        windows_split_vertical();
    else
        windows_split_horizontal();
    Window *w = window_cur();
    if (w)
        win_attach_buf(w, &E.buffers[idx]);
    E.buffers[idx].dirty = 0;
    E.current_buffer = idx;
    return 0;
}

Window *buf_special_show_modal(int idx, int anchor_x, int anchor_y) {
    if (!valid_idx(idx))
        return NULL;
    Buffer *b = &E.buffers[idx];
    b->dirty = 0;

    int w = 0;
    for (int i = 0; i < b->num_rows; i++) {
        int c = utf8_display_width(b->rows[i].chars.data, b->rows[i].chars.len);
        if (c > w)
            w = c;
    }
    w += 2;
    if (w < 10)
        w = 10;
    if (w > E.screen_cols - 6)
        w = E.screen_cols - 6;
    int h = b->num_rows > 0 ? b->num_rows : 1;
    if (h > E.screen_rows - 6)
        h = E.screen_rows - 6;

    Window *m =
        (anchor_x >= 0 && anchor_y >= 0)
            ? winmodal_create_anchored(anchor_x, anchor_y, w, h, WMODAL_AUTO)
            : winmodal_create(-1, -1, w, h);
    if (!m) {
        buf_special_close(idx);
        return NULL;
    }
    m->buffer_index = idx;
    winmodal_show(m);
    return m;
}

int buf_special_modal_key(Window *modal, int key, int allow_close) {
    if (!modal || !valid_idx(modal->buffer_index))
        return 0;
    Buffer *b = &E.buffers[modal->buffer_index];
    if (allow_close && (key == 'q' || key == '\x1b')) {
        buf_special_close(modal->buffer_index);
        return 2;
    }
    if (key == 'j' || key == KEY_ARROW_DOWN) {
        int max_off = b->num_rows - modal->height;
        if (max_off < 0)
            max_off = 0;
        if (modal->row_offset < max_off)
            modal->row_offset++;
        return 1;
    }
    if (key == 'k' || key == KEY_ARROW_UP) {
        if (modal->row_offset > 0)
            modal->row_offset--;
        return 1;
    }
    return 0;
}

void buf_special_close(int idx) {
    if (!valid_idx(idx))
        return;
    Window *m = winmodal_current();
    if (m && m->buffer_index == idx) {
        winmodal_hide(m);
        winmodal_destroy(m);
    }
    E.buffers[idx].dirty = 0;
    buf_close(idx);
}
