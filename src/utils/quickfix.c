#include "hed.h"

#define QF_BUFFER_FILETYPE "quickfix"
#define QF_BUFFER_TITLE    "[Quickfix]"

/* Find the index of the quickfix buffer (if any). */
static int qf_find_buffer_index(void) {
    for (int i = 0; i < (int)E.buffers.len; i++) {
        Buffer *b = &E.buffers.data[i];
        if (!b) continue;
        if (b->filetype &&
            strcmp(b->filetype, QF_BUFFER_FILETYPE) == 0 &&
            b->filename == NULL) {
            return i;
        }
    }
    return -1;
}

int qf_is_quickfix_buffer(const Buffer *buf) {
    if (!buf || !buf->filetype) return 0;
    if (buf->filename != NULL) return 0;
    return strcmp(buf->filetype, QF_BUFFER_FILETYPE) == 0;
}

Buffer *qf_get_buffer(Qf *qf) {
    (void)qf;
    int idx = qf_find_buffer_index();
    if (idx < 0) return NULL;
    return &E.buffers.data[idx];
}

Buffer *qf_get_or_create_buffer(Qf *qf) {
    Buffer *buf = qf_get_buffer(qf);
    if (buf) return buf;

    int idx = -1;
    if (buf_new(NULL, &idx) != ED_OK) {
        ed_set_status_message("Quickfix: failed to create buffer");
        return NULL;
    }
    if (!BOUNDS_CHECK(idx, E.buffers.len)) return NULL;
    buf = &E.buffers.data[idx];

    if (buf->title) free(buf->title);
    buf->title = strdup(QF_BUFFER_TITLE);
    if (!buf->title) buf->title = strdup("[No Name]");

    if (buf->filetype) free(buf->filetype);
    buf->filetype = strdup(QF_BUFFER_FILETYPE);

    buf->readonly = 1;
    return buf;
}

/* Keep the quickfix window's cursor/scroll aligned with qf->sel. */
static void qf_update_window_view(Qf *qf) {
    if (!qf) return;
    int buf_index = qf_find_buffer_index();
    if (buf_index < 0) return;
    Buffer *buf = &E.buffers.data[buf_index];
    if (!buf) return;

    int sel = qf->sel;
    if (sel < 0) sel = 0;
    if (sel >= buf->num_rows) sel = buf->num_rows - 1;
    if (sel < 0) sel = 0;

    for (int wi = 0; wi < (int)E.windows.len; wi++) {
        Window *w = &E.windows.data[wi];
        if (w->buffer_index == buf_index && w->is_quickfix) {
            w->cursor.y = sel;
            w->cursor.x = 0;
            if (w->height > 0) {
                if (sel < w->row_offset) w->row_offset = sel;
                else if (sel >= w->row_offset + w->height) w->row_offset = sel - w->height + 1;
                if (w->row_offset < 0) w->row_offset = 0;
            }
            qf->scroll = w->row_offset;
        }
    }
}

/* Local helper to insert a row into a buffer (mirrors buf_row_insert_in). */
static void qf_buf_row_insert(Buffer *buf, int at, const char *s, size_t len) {
    if (!buf) return;
    if (at < 0 || at > buf->num_rows) return;

    Row *new_rows = realloc(buf->rows, sizeof(Row) * (buf->num_rows + 1));
    if (!new_rows) {
        ed_set_status_message("Quickfix: out of memory");
        return;
    }
    buf->rows = new_rows;
    memmove(&buf->rows[at + 1], &buf->rows[at],
            sizeof(Row) * (buf->num_rows - at));

    buf->rows[at].chars = sstr_from(s, len);
    buf->rows[at].render = sstr_new();
    buf_row_update(&buf->rows[at]);

    buf->num_rows++;
    buf->dirty++;

    HookLineEvent event = {buf, at, s, len};
    hook_fire_line(HOOK_LINE_INSERT, &event);
}

/* Rebuild the quickfix buffer contents from the Qf items. */
static void qf_sync_buffer(Qf *qf) {
    if (!qf) return;
    Buffer *buf = qf_get_or_create_buffer(qf);
    if (!buf) return;

    /* Clear existing rows */
    for (int i = 0; i < buf->num_rows; i++) {
        row_free(&buf->rows[i]);
    }
    free(buf->rows);
    buf->rows = NULL;
    buf->num_rows = 0;

    /* Rebuild from items */
    for (int i = 0; i < qf->len; i++) {
        const QfItem *it = &qf->items[i];
        char line[512];
        int l;
        if (it->filename && it->filename[0]) {
            l = snprintf(line, sizeof(line), "%s:%d:%d: %s",
                         it->filename, it->line, it->col,
                         it->text ? it->text : "");
        } else {
            l = snprintf(line, sizeof(line), "%d:%d: %s",
                         it->line, it->col,
                         it->text ? it->text : "");
        }
        if (l < 0) l = 0;
        if (l > (int)sizeof(line)) l = (int)sizeof(line);
        qf_buf_row_insert(buf, buf->num_rows, line, (size_t)l);
    }

    /* Quickfix buffer is virtual; don't mark it dirty. */
    buf->dirty = 0;

    /* Keep cursor/scroll in sync with selection. */
    qf_update_window_view(qf);
}

static void qf_item_free(QfItem *it) {
    if (!it) return;
    free(it->text); it->text = NULL;
    free(it->filename); it->filename = NULL;
}

void qf_init(Qf *qf) {
    if (!qf) return;
    qf->open = 0; qf->focus = 0; qf->height = 8; qf->sel = 0; qf->scroll = 0;
    qf->items = NULL; qf->len = 0; qf->cap = 0;
}

void qf_free(Qf *qf) {
    if (!qf) return;
    for (int i = 0; i < qf->len; i++) qf_item_free(&qf->items[i]);
    free(qf->items); qf->items = NULL; qf->len = qf->cap = 0;
}

static void qf_reserve(Qf *qf, int need) {
    if (qf->cap >= need) return;
    int ncap = qf->cap ? qf->cap * 2 : 32;
    if (ncap < need) ncap = need;
    qf->items = realloc(qf->items, (size_t)ncap * sizeof(QfItem));
    qf->cap = ncap;
}

void qf_open(Qf *qf, int height) {
    if (!qf) return;
    if (height > 0) qf->height = height;

    /* Ensure backing buffer exists and is up to date */
    qf_sync_buffer(qf);

    Buffer *buf = qf_get_buffer(qf);
    if (!buf) return;
    int buf_index = qf_find_buffer_index();

    /* Try to find an existing quickfix window showing this buffer */
    int qf_win_idx = -1;
    for (int wi = 0; wi < (int)E.windows.len; wi++) {
        Window *w = &E.windows.data[wi];
        if (w->buffer_index == buf_index && w->is_quickfix) {
            qf_win_idx = wi;
            break;
        }
    }

    if (qf_win_idx < 0) {
        /* Create a new horizontal split like a normal buffer window */
        Window *cur = window_cur();
        if (!cur) {
            qf->open = 1;
            return;
        }
        windows_split_horizontal();
        Window *w = window_cur();
        if (!w) {
            qf->open = 1;
            return;
        }
        w->is_quickfix = 1;
        win_attach_buf(w, buf);
        buf_index = (int)(buf - E.buffers.data);
        qf_win_idx = (int)(w - E.windows.data);
    }

    /* Focus the quickfix window */
    if (qf_win_idx >= 0 && qf_win_idx < (int)E.windows.len) {
        for (int i = 0; i < (int)E.windows.len; i++) {
            E.windows.data[i].focus = 0;
        }
        E.current_window = qf_win_idx;
        E.windows.data[qf_win_idx].focus = 1;
        if (BOUNDS_CHECK(buf_index, E.buffers.len)) {
            E.current_buffer = buf_index;
        }
    }

    qf->open = 1;
    qf->focus = 0;
    if (qf->sel >= qf->len) qf->sel = qf->len ? qf->len - 1 : 0;
    qf_update_window_view(qf);
}

void qf_close(Qf *qf) {
    if (!qf) return;
    int buf_index = qf_find_buffer_index();
    if (buf_index >= 0) {
        /* Close any windows that are marked as quickfix and show this buffer.
         * Iterate from the end since windows_close_current() shifts the array. */
        for (int wi = (int)E.windows.len - 1; wi >= 0; wi--) {
            if (wi >= (int)E.windows.len) continue;
            Window *w = &E.windows.data[wi];
            if (w->is_quickfix && w->buffer_index == buf_index) {
                int prev = E.current_window;
                E.current_window = wi;
                windows_close_current();
                /* windows_close_current() may change E.current_window; that's fine. */
                if (prev == wi && E.current_window == wi && E.windows.len == 1) {
                    /* Can't close the last window; stop trying. */
                    break;
                }
            }
        }
    }
    qf->open = 0; qf->focus = 0;
}

void qf_toggle(Qf *qf, int height) {
    if (!qf) return;
    if (qf->open) qf_close(qf); else qf_open(qf, height);
}

void qf_focus(Qf *qf) { if (qf) qf->focus = 1; }
void qf_blur(Qf *qf)  { if (qf) qf->focus = 0; }

void qf_clear(Qf *qf) {
    if (!qf) return;
    for (int i = 0; i < qf->len; i++) qf_item_free(&qf->items[i]);
    qf->len = 0; qf->sel = 0; qf->scroll = 0;
    if (qf->open) qf_sync_buffer(qf);
}

int qf_add(Qf *qf, const char *filename, int line, int col, const char *text) {
    if (!qf) return -1;
    qf_reserve(qf, qf->len + 1);
    QfItem *it = &qf->items[qf->len++];
    it->text = text ? strdup(text) : strdup("");
    it->filename = filename ? strdup(filename) : NULL;
    it->line = line; it->col = col;
    if (qf->open) qf_sync_buffer(qf);
    return qf->len - 1;
}

void qf_move(Qf *qf, int delta) {
    if (!qf || qf->len == 0) return;
    int ns = qf->sel + delta;
    if (ns < 0) ns = 0;
    if (ns >= qf->len) ns = qf->len - 1;
    qf->sel = ns;
    qf_update_window_view(qf);
}

static void qf_jump_to(const QfItem *it) {
    if (!it) return;
    if (it->filename) {
        Buffer *nb = NULL;
        EdError err = buf_open_file(it->filename, &nb);
        if ((err == ED_OK || err == ED_ERR_FILE_NOT_FOUND) && nb) {
            win_attach_buf(window_cur(), nb);
        }
    }
    Buffer *b = buf_cur(); Window *win = window_cur();
    if (!b || !win) return;
    if (it->line > 0) {
        buf_goto_line(it->line);
    }
    if (it->col > 0) {
        if (win->cursor.y < b->num_rows) {
            int max = b->rows[win->cursor.y].chars.len;
            int cx = it->col - 1; if (cx < 0) cx = 0; if (cx > max) cx = max;
            win->cursor.x = cx;
        }
    }
}

void qf_open_selected(Qf *qf) {
    if (!qf || qf->len == 0) return;
    qf_update_window_view(qf);
    qf_jump_to(&qf->items[qf->sel]);
}

void qf_open_idx(Qf *qf, int idx) {
    if (!qf || idx < 0 || idx >= qf->len) return;
    qf->sel = idx;
    qf_update_window_view(qf);
    qf_jump_to(&qf->items[idx]);
}
