#include "hed.h"

#define QF_BUFFER_FILETYPE "quickfix"
#define QF_BUFFER_TITLE "[Quickfix]"

/* Find the index of the quickfix buffer (if any). */
static int qf_find_buffer_index(void) {
    for (int i = 0; i < (int)E.buffers.len; i++) {
        Buffer *b = &E.buffers.data[i];
        if (!b)
            continue;
        if (b->filetype && strcmp(b->filetype, QF_BUFFER_FILETYPE) == 0 &&
            b->filename == NULL) {
            return i;
        }
    }
    return -1;
}

int qf_is_quickfix_buffer(const Buffer *buf) {
    if (!buf || !buf->filetype)
        return 0;
    if (buf->filename != NULL)
        return 0;
    return strcmp(buf->filetype, QF_BUFFER_FILETYPE) == 0;
}

Buffer *qf_get_buffer(Qf *qf) {
    (void)qf;
    int idx = qf_find_buffer_index();
    if (idx < 0)
        return NULL;
    return &E.buffers.data[idx];
}

Buffer *qf_get_or_create_buffer(Qf *qf) {
    Buffer *buf = qf_get_buffer(qf);
    if (buf)
        return buf;

    int idx = -1;
    if (buf_new(NULL, &idx) != ED_OK) {
        ed_set_status_message("Quickfix: failed to create buffer");
        return NULL;
    }
    if (!BOUNDS_CHECK(idx, E.buffers.len))
        return NULL;
    buf = &E.buffers.data[idx];

    if (buf->title)
        free(buf->title);
    buf->title = strdup(QF_BUFFER_TITLE);
    if (!buf->title)
        buf->title = strdup("[No Name]");

    if (buf->filetype)
        free(buf->filetype);
    buf->filetype = strdup(QF_BUFFER_FILETYPE);

    buf->readonly = 1;
    return buf;
}

/* Keep the quickfix window's cursor/scroll aligned with qf->sel and
 * visually mark the selected item in the quickfix buffer. */
static void qf_update_window_view(Qf *qf) {
    if (!qf)
        return;
    int buf_index = qf_find_buffer_index();
    if (buf_index < 0)
        return;
    Buffer *buf = &E.buffers.data[buf_index];
    if (!buf)
        return;

    int sel = qf->sel;
    if (buf->num_rows <= 0) {
        sel = -1; /* No rows => no visible selection marker */
    } else {
        if (sel < 0)
            sel = 0;
        if (sel >= buf->num_rows)
            sel = buf->num_rows - 1;
    }

    /* Update '*' marker on the selected row (if any). The quickfix buffer
     * lines are constructed with a two-character prefix ("* " or "  "). */
    if (buf->num_rows > 0) {
        for (int i = 0; i < buf->num_rows; i++) {
            Row *row = &buf->rows[i];
            if (!row->chars.data || row->chars.len == 0)
                continue;
            char desired = (i == sel) ? '*' : ' ';
            if (row->chars.data[0] != desired) {
                row->chars.data[0] = desired;
                buf_row_update(row);
            }
        }
    }

    int cursor_row = (sel < 0) ? 0 : sel;

    for (int wi = 0; wi < (int)E.windows.len; wi++) {
        Window *w = &E.windows.data[wi];
        if (w->buffer_index == buf_index && w->is_quickfix) {
            buf->cursor.y = cursor_row;
            buf->cursor.x = 0;
            if (w->height > 0) {
                if (cursor_row < w->row_offset)
                    w->row_offset = cursor_row;
                else if (cursor_row >= w->row_offset + w->height)
                    w->row_offset = cursor_row - w->height + 1;
                if (w->row_offset < 0)
                    w->row_offset = 0;
            }
            qf->scroll = w->row_offset;
        }
    }
}

/* Public wrapper so other modules (e.g., hooks) can resync the quickfix
 * window/markers after updating E.qf.sel. */
void qf_update_view(Qf *qf) { qf_update_window_view(qf); }

/* Local helper to insert a row into a buffer (mirrors buf_row_insert_in). */
static void qf_buf_row_insert(Buffer *buf, int at, const char *s, size_t len) {
    if (!buf)
        return;
    if (at < 0 || at > buf->num_rows)
        return;

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
    if (!qf)
        return;
    Buffer *buf = qf_get_or_create_buffer(qf);
    if (!buf)
        return;

    /* Clear existing rows */
    for (int i = 0; i < buf->num_rows; i++) {
        row_free(&buf->rows[i]);
    }
    free(buf->rows);
    buf->rows = NULL;
    buf->num_rows = 0;

    /* Rebuild from items */
    int sel = (qf->sel >= 0 && qf->sel < (int)qf->items.len) ? qf->sel : -1;
    for (int i = 0; i < (int)qf->items.len; i++) {
        const QfItem *it = &qf->items.data[i];
        char line[512];
        int l;
        if (it->filename && it->filename[0]) {
            l = snprintf(line, sizeof(line), "%c %s:%d:%d: %s",
                         (i == sel) ? '*' : ' ', it->filename, it->line,
                         it->col, it->text ? it->text : "");
        } else {
            l = snprintf(line, sizeof(line), "%c %d:%d: %s",
                         (i == sel) ? '*' : ' ', it->line, it->col,
                         it->text ? it->text : "");
        }
        if (l < 0)
            l = 0;
        if (l > (int)sizeof(line))
            l = (int)sizeof(line);
        qf_buf_row_insert(buf, buf->num_rows, line, (size_t)l);
    }

    /* Quickfix buffer is virtual; don't mark it dirty. */
    buf->dirty = 0;

    /* Keep cursor/scroll in sync with selection. */
    qf_update_window_view(qf);
}

static void qf_item_free(QfItem *it) {
    if (!it)
        return;
    free(it->text);
    it->text = NULL;
    free(it->filename);
    it->filename = NULL;
}

void qf_init(Qf *qf) {
    if (!qf)
        return;
    qf->open = 0;
    qf->focus = 0;
    qf->height = 8;
    qf->sel = 0;
    qf->scroll = 0;
    qf->items.data = NULL;
    qf->items.len = 0;
    qf->items.cap = 0;
}

void qf_free(Qf *qf) {
    if (!qf)
        return;
    for (size_t i = 0; i < qf->items.len; i++)
        qf_item_free(&qf->items.data[i]);
    free(qf->items.data);
    qf->items.data = NULL;
    qf->items.len = 0;
    qf->items.cap = 0;
}

void qf_open(Qf *qf, int height) {
    if (!qf)
        return;
    if (height > 0)
        qf->height = height;

    /* Ensure backing buffer exists and is up to date */
    qf_sync_buffer(qf);

    Buffer *buf = qf_get_buffer(qf);
    if (!buf)
        return;
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
    if (qf->sel >= (int)qf->items.len)
        qf->sel = qf->items.len ? (int)qf->items.len - 1 : 0;
    qf_update_window_view(qf);
}

void qf_close(Qf *qf) {
    if (!qf)
        return;
    int buf_index = qf_find_buffer_index();
    if (buf_index >= 0) {
        /* Close any windows that are marked as quickfix and show this buffer.
         * Iterate from the end since windows_close_current() shifts the array.
         */
        for (int wi = (int)E.windows.len - 1; wi >= 0; wi--) {
            if (wi >= (int)E.windows.len)
                continue;
            Window *w = &E.windows.data[wi];
            if (w->is_quickfix && w->buffer_index == buf_index) {
                int prev = E.current_window;
                E.current_window = wi;
                windows_close_current();
                /* windows_close_current() may change E.current_window; that's
                 * fine. */
                if (prev == wi && E.current_window == wi &&
                    E.windows.len == 1) {
                    /* Can't close the last window; stop trying. */
                    break;
                }
            }
        }
    }
    qf->open = 0;
    qf->focus = 0;
}

void qf_toggle(Qf *qf, int height) {
    if (!qf)
        return;
    if (qf->open)
        qf_close(qf);
    else
        qf_open(qf, height);
}

void qf_focus(Qf *qf) {
    if (qf)
        qf->focus = 1;
}
void qf_blur(Qf *qf) {
    if (qf)
        qf->focus = 0;
}

void qf_clear(Qf *qf) {
    if (!qf)
        return;
    for (size_t i = 0; i < qf->items.len; i++)
        qf_item_free(&qf->items.data[i]);
    qf->items.len = 0;
    qf->sel = 0;
    qf->scroll = 0;
    if (qf->open)
        qf_sync_buffer(qf);
}

int qf_add(Qf *qf, const char *filename, int line, int col, const char *text) {
    if (!qf)
        return -1;

    size_t old_len = qf->items.len;
    QfItem item = {
        .text = text ? strdup(text) : strdup(""),
        .filename = filename ? strdup(filename) : NULL,
        .line = line,
        .col = col
    };
    vec_push_typed(&qf->items, QfItem, item);

    /* Check if push succeeded */
    if (qf->items.len == old_len) {
        /* Push failed - clean up allocated memory */
        free(item.text);
        free(item.filename);
        return -1;
    }

    if (qf->open)
        qf_sync_buffer(qf);
    return (int)qf->items.len - 1;
}

void qf_move(Qf *qf, int delta) {
    if (!qf || qf->items.len == 0)
        return;
    int ns = qf->sel + delta;
    if (ns < 0)
        ns = 0;
    if (ns >= (int)qf->items.len)
        ns = (int)qf->items.len - 1;
    qf->sel = ns;
    qf_update_window_view(qf);
}

/* Find the best target window for opening a quickfix item.
 * - If the current window is not a quickfix pane, use it.
 * - If the current window *is* the quickfix pane, prefer a non-quickfix window
 *   directly above it (with horizontal overlap). If none exists, fall back to
 *   any non-quickfix window. If still none exists, create a new horizontal
 *   split from the quickfix window and use the window above as the target,
 *   keeping the quickfix pane below. */
static int qf_pick_target_window_index(void) {
    if (E.windows.len == 0)
        return -1;
    if (!BOUNDS_CHECK(E.current_window, E.windows.len))
        return -1;

    Window *cur = window_cur();
    if (!cur)
        return -1;

    /* If we're not in a quickfix window, just use the current window. */
    if (!cur->is_quickfix) {
        return E.current_window;
    }

    int cur_idx = E.current_window;

    /* Try to find a non-quickfix window above the quickfix pane, overlapping
     * horizontally. */
    int qf_top = cur->top;
    int qf_left = cur->left;
    int qf_right = cur->left + cur->width - 1;

    int best_idx = -1;
    int best_bottom = -1;

    for (int i = 0; i < (int)E.windows.len; i++) {
        if (i == cur_idx)
            continue;
        Window *w = &E.windows.data[i];
        if (w->is_quickfix)
            continue;

        int w_bottom = w->top + w->height - 1;
        int w_left = w->left;
        int w_right = w->left + w->width - 1;

        /* Must be strictly above and horizontally overlapping. */
        if (w_bottom < qf_top && !(w_right < qf_left || w_left > qf_right)) {
            if (best_idx < 0 || w_bottom > best_bottom) {
                best_idx = i;
                best_bottom = w_bottom;
            }
        }
    }

    if (best_idx >= 0) {
        return best_idx;
    }

    /* Fallback: use any non-quickfix window. */
    for (int i = 0; i < (int)E.windows.len; i++) {
        if (i == cur_idx)
            continue;
        if (!E.windows.data[i].is_quickfix) {
            return i;
        }
    }

    /* No non-quickfix window exists: create one by splitting the quickfix
     * window. We split horizontally and treat the original window (cur_idx) as
     * the one above, and the new window as the quickfix pane below. */
    int prev_idx = cur_idx;
    windows_split_horizontal();
    if (!BOUNDS_CHECK(prev_idx, E.windows.len))
        return prev_idx;

    int new_idx = (int)E.windows.len - 1;
    if (!BOUNDS_CHECK(new_idx, E.windows.len))
        return prev_idx;

    Window *top = &E.windows.data[prev_idx];
    Window *bottom = &E.windows.data[new_idx];

    /* Ensure the bottom window remains the quickfix pane. */
    bottom->is_quickfix = 1;
    top->is_quickfix = 0;

    /* Use the window above (top) as the jump target. */
    return prev_idx;
}

/* Focus the given window index and sync E.current_buffer. */
static void qf_focus_window_index(int idx) {
    if (!BOUNDS_CHECK(idx, E.windows.len))
        return;
    for (int i = 0; i < (int)E.windows.len; i++) {
        E.windows.data[i].focus = 0;
    }
    E.current_window = idx;
    E.windows.data[idx].focus = 1;
    int bi = E.windows.data[idx].buffer_index;
    if (BOUNDS_CHECK(bi, E.buffers.len)) {
        E.current_buffer = bi;
    }
}

/* Internal helper: open the given item in the target window.
 * If focus_target is non-zero, move focus to that window (used by
 * :cnext/:cprev and friends). If zero, preview the item in the
 * target window but keep focus/cursor on the quickfix pane. */
static void qf_jump_to_internal(const QfItem *it, int focus_target) {
    if (!it)
        return;
    int target_idx = qf_pick_target_window_index();
    if (target_idx < 0)
        return;

    int saved_window = E.current_window;
    int saved_buffer = E.current_buffer;

    if (focus_target) {
        /* Normal jump: move focus to the destination window. */
        qf_focus_window_index(target_idx);
    } else {
        /* Preview: keep quickfix focused, but temporarily treat the
         * target window as current for buffer operations. */
        E.current_window = target_idx;
        if (BOUNDS_CHECK(target_idx, E.windows.len)) {
            int bi = E.windows.data[target_idx].buffer_index;
            if (BOUNDS_CHECK(bi, E.buffers.len)) {
                E.current_buffer = bi;
            }
        }
    }

    if (it->filename && it->filename[0]) {
        /* Reuse existing buffer for this filename if present; otherwise open
         * it. */
        buf_open_or_switch(it->filename, true);
    }

    Buffer *b = buf_cur();
    Window *win = window_cur();
    if (b && win) {
        if (it->line > 0) {
            buf_goto_line(it->line);
        }
        if (it->col > 0 && b->cursor.y < b->num_rows) {
            int max = b->rows[b->cursor.y].chars.len;
            int cx = it->col - 1;
            if (cx < 0)
                cx = 0;
            if (cx > max)
                cx = max;
            b->cursor.x = cx;
        }
    }

    if (!focus_target) {
        /* Restore original current window/buffer so that input focus
         * and cursor stay on the quickfix pane. */
        E.current_window = saved_window;
        E.current_buffer = saved_buffer;
    }
}

void qf_open_selected(Qf *qf) {
    if (!qf || qf->items.len == 0)
        return;
    qf_update_window_view(qf);
    qf_jump_to_internal(&qf->items.data[qf->sel], 1);
}

void qf_preview_selected(Qf *qf) {
    if (!qf || qf->items.len == 0)
        return;
    qf_update_window_view(qf);
    qf_jump_to_internal(&qf->items.data[qf->sel], 0);
}

void qf_open_idx(Qf *qf, int idx) {
    if (!qf || idx < 0 || idx >= (int)qf->items.len)
        return;
    qf->sel = idx;
    qf_update_window_view(qf);
    qf_jump_to_internal(&qf->items.data[idx], 1);
}
