#include "buf/cursors.h"
#include "editor.h"
#include "stb_ds.h"
#include <stdlib.h>

/*** Multi-cursor list management ***/

Cursor *buf_cursor_add(Buffer *buf, int y, int x) {
    if (!buf)
        return NULL;
    Cursor *c = calloc(1, sizeof(Cursor));
    if (!c)
        return NULL;
    c->x = x;
    c->y = y;
    arrput(buf->all_cursors, c);
    return c;
}

int buf_cursor_remove(Buffer *buf, Cursor *c) {
    if (!buf || !c)
        return 0;
    if (c == buf->cursor)
        return 0;
    for (ptrdiff_t i = 0; i < arrlen(buf->all_cursors); i++) {
        if (buf->all_cursors[i] == c) {
            free(c);
            arrdel(buf->all_cursors, i);
            return 1;
        }
    }
    return 0;
}

void buf_cursor_clear_extras(Buffer *buf) {
    if (!buf || !buf->cursor)
        return;
    Cursor *keep = buf->cursor;
    for (ptrdiff_t i = 0; i < arrlen(buf->all_cursors); i++) {
        if (buf->all_cursors[i] != keep)
            free(buf->all_cursors[i]);
    }
    buf->all_cursors[0] = keep;
    arrsetlen(buf->all_cursors, 1);
}

int buf_cursor_set_active(Buffer *buf, Cursor *c) {
    if (!buf || !c)
        return 0;
    for (ptrdiff_t i = 0; i < arrlen(buf->all_cursors); i++) {
        if (buf->all_cursors[i] == c) {
            buf->cursor = c;
            return 1;
        }
    }
    return 0;
}

int buf_cursor_count(const Buffer *buf) {
    return buf ? (int)arrlen(buf->all_cursors) : 0;
}

void buf_cursor_sync_from_window(Buffer *buf) {
    Window *win = window_cur();
    if (!buf || !win || !buf->cursor)
        return;
    /* Only sync when the focused window shows this buffer. */
    if (win->buffer_index < 0 || win->buffer_index >= (int)arrlen(E.buffers))
        return;
    if (&E.buffers[win->buffer_index] != buf)
        return;
    buf->cursor->x = win->cursor.x;
    buf->cursor->y = win->cursor.y;
}

/*** Per-(buffer, window) cursor sets ***/

static void cursor_set_free(CursorSet *set) {
    if (!set)
        return;
    for (ptrdiff_t i = 0; i < arrlen(set->cursors); i++)
        free(set->cursors[i]);
    arrfree(set->cursors);
    set->cursors = NULL;
    set->active = NULL;
}

/* Park the live set under its current owner id. */
static void cursors_stash_live(Buffer *buf) {
    if (!buf->all_cursors)
        return;
    CursorSet set = {
        .win_id = buf->cursor_win_id,
        .cursors = buf->all_cursors,
        .active = buf->cursor,
    };
    arrput(buf->cursor_sets, set);
    buf->all_cursors = NULL;
    buf->cursor = NULL;
}

void buf_cursors_bind_window(Buffer *buf, struct Window *win) {
    if (!buf || !win || win->is_modal || win->id <= 0)
        return;
    if (buf->cursor_win_id == win->id)
        return;
    if (buf < E.buffers || buf >= E.buffers + arrlen(E.buffers))
        return;
    int buf_idx = (int)(buf - E.buffers);

    /* Capture the previous owner's window position into the live set
     * before parking it — its window cursor is the source of truth. */
    Window *owner = window_find_by_id(buf->cursor_win_id);
    int owner_alive =
        owner && !owner->is_modal && owner->buffer_index == buf_idx;
    if (owner_alive && buf->cursor) {
        buf->cursor->x = owner->cursor.x;
        buf->cursor->y = owner->cursor.y;
    }

    /* Drop parked sets whose window is gone or shows another buffer.
     * (The "last window" set a buffer keeps is the live one, never a
     * parked one, so this is pure garbage collection.) */
    for (ptrdiff_t i = arrlen(buf->cursor_sets) - 1; i >= 0; i--) {
        Window *w = window_find_by_id(buf->cursor_sets[i].win_id);
        if (!w || w->is_modal || w->buffer_index != buf_idx) {
            cursor_set_free(&buf->cursor_sets[i]);
            arrdel(buf->cursor_sets, i);
        }
    }

    ptrdiff_t mine = -1;
    for (ptrdiff_t i = 0; i < arrlen(buf->cursor_sets); i++) {
        if (buf->cursor_sets[i].win_id == win->id) {
            mine = i;
            break;
        }
    }

    if (mine >= 0) {
        /* Restore the set previously parked for this window. */
        CursorSet incoming = buf->cursor_sets[mine];
        arrdel(buf->cursor_sets, mine);
        if (owner_alive)
            cursors_stash_live(buf);
        else {
            for (ptrdiff_t i = 0; i < arrlen(buf->all_cursors); i++)
                free(buf->all_cursors[i]);
            arrfree(buf->all_cursors);
        }
        buf->all_cursors = incoming.cursors;
        buf->cursor =
            incoming.active
                ? incoming.active
                : (arrlen(incoming.cursors) > 0 ? incoming.cursors[0] : NULL);
    } else if (owner_alive) {
        /* The owner window still shows this buffer and keeps its set;
         * the new pair starts fresh with a single cursor where the
         * live set's active cursor is (vim-like second view). */
        int sy = buf->cursor ? buf->cursor->y : 0;
        int sx = buf->cursor ? buf->cursor->x : 0;
        cursors_stash_live(buf);
        Cursor *c0 = calloc(1, sizeof(Cursor));
        if (c0) {
            c0->y = sy;
            c0->x = sx;
            arrput(buf->all_cursors, c0);
            buf->cursor = c0;
        }
    }
    /* else: previous owner is gone — the live set is the buffer's
     * "last window" set and this window adopts it unchanged. */

    buf->cursor_win_id = win->id;
}

CursorVec buf_cursors_for_window(Buffer *buf, const struct Window *win,
                                 Cursor **skip_active) {
    if (skip_active)
        *skip_active = NULL;
    if (!buf || !win || win->id <= 0)
        return NULL;
    if (buf->cursor_win_id == win->id) {
        if (skip_active)
            *skip_active = buf->cursor;
        return buf->all_cursors;
    }
    for (ptrdiff_t i = 0; i < arrlen(buf->cursor_sets); i++) {
        if (buf->cursor_sets[i].win_id == win->id)
            return buf->cursor_sets[i].cursors;
    }
    return NULL;
}

/*** Auto-shift helpers — applied after edits to every cursor in the
 * buffer except the live active one (buf->cursor), which is updated
 * separately via buf_cursor_sync_from_window(). Parked sets (other
 * windows / last-window leftovers) shift too so their cursors stay
 * glued to the text they were placed on. ***/

/* Run `fn(cursor, a, b)` over the live set (skipping the active
 * cursor) and over every parked set (no skips — parked actives are
 * not window-synced, they must shift like any other cursor). */
static void cursors_shift_all(Buffer *buf,
                              void (*fn)(Buffer *, Cursor *, int, int), int a,
                              int b) {
    if (!buf)
        return;
    for (ptrdiff_t i = 0; i < arrlen(buf->all_cursors); i++) {
        Cursor *c = buf->all_cursors[i];
        if (c == buf->cursor)
            continue;
        fn(buf, c, a, b);
    }
    for (ptrdiff_t s = 0; s < arrlen(buf->cursor_sets); s++) {
        CursorVec v = buf->cursor_sets[s].cursors;
        for (ptrdiff_t i = 0; i < arrlen(v); i++)
            fn(buf, v[i], a, b);
    }
}

static void shift_insert_char(Buffer *buf, Cursor *c, int iy, int ix) {
    (void)buf;
    if (c->y == iy && c->x >= ix)
        c->x++;
}

static void shift_delete_char(Buffer *buf, Cursor *c, int iy, int ix) {
    (void)buf;
    if (c->y == iy && c->x > ix)
        c->x--;
}

static void shift_insert_newline(Buffer *buf, Cursor *c, int iy, int ix) {
    (void)buf;
    if (c->y > iy) {
        c->y++;
    } else if (c->y == iy && c->x >= ix) {
        c->y++;
        c->x -= ix;
    }
}

static void shift_join_lines(Buffer *buf, Cursor *c, int iy, int join_at) {
    (void)buf;
    if (c->y == iy) {
        c->y--;
        c->x += join_at;
    } else if (c->y > iy) {
        c->y--;
    }
}

static void shift_delete_line(Buffer *buf, Cursor *c, int iy, int unused) {
    (void)unused;
    if (c->y > iy) {
        c->y--;
    } else if (c->y == iy) {
        /* Row gone; cursor lands on what's now at iy (or clamps). */
        if (c->y >= buf->num_rows)
            c->y = buf->num_rows > 0 ? buf->num_rows - 1 : 0;
        c->x = 0;
    }
}

void cursors_after_insert_char(Buffer *buf, int iy, int ix) {
    cursors_shift_all(buf, shift_insert_char, iy, ix);
}

void cursors_after_delete_char(Buffer *buf, int iy, int ix) {
    cursors_shift_all(buf, shift_delete_char, iy, ix);
}

void cursors_after_insert_newline(Buffer *buf, int iy, int ix) {
    cursors_shift_all(buf, shift_insert_newline, iy, ix);
}

void cursors_after_join_lines(Buffer *buf, int iy, int join_at) {
    cursors_shift_all(buf, shift_join_lines, iy, join_at);
}

void cursors_after_delete_line(Buffer *buf, int iy) {
    cursors_shift_all(buf, shift_delete_line, iy, 0);
}
