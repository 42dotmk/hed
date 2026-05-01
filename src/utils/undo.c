#include "utils/undo.h"
#include "buf/buffer.h"
#include "editor.h"
#include "hooks.h"
#include "lib/log.h"
#include "buf/row.h"
#include <stdlib.h>
#include <string.h>

/* Forward declarations of buffer primitives we drive during apply.
 * (The full prototypes live in buffer.c / row.c respectively.) */
void buf_row_insert_in(Buffer *buf, int at, const char *s, size_t len);
void buf_row_del_in(Buffer *buf, int at);
void buf_row_update(Row *row);

/* ===================================================================
 * Group/record memory management
 * =================================================================== */

static void rec_clear(UndoRec *r) {
    if (!r)
        return;
    sstr_free(&r->data);
}

static void group_free(UndoGroup *g) {
    if (!g)
        return;
    for (int i = 0; i < g->len; i++)
        rec_clear(&g->recs[i]);
    free(g->recs);
    free(g);
}

static UndoRec *group_add_rec(UndoGroup *g) {
    if (g->len == g->cap) {
        int nc = g->cap ? g->cap * 2 : 8;
        UndoRec *nr = realloc(g->recs, (size_t)nc * sizeof(UndoRec));
        if (!nr)
            return NULL;
        g->recs = nr;
        g->cap = nc;
    }
    UndoRec *r = &g->recs[g->len++];
    memset(r, 0, sizeof(*r));
    return r;
}

static void stack_push(UndoGroup ***stack, int *len, int *cap, UndoGroup *g) {
    if (*len == *cap) {
        int nc = *cap ? *cap * 2 : 16;
        UndoGroup **ns =
            realloc(*stack, (size_t)nc * sizeof(UndoGroup *));
        if (!ns) {
            group_free(g);
            return;
        }
        *stack = ns;
        *cap = nc;
    }
    (*stack)[(*len)++] = g;
}

static UndoGroup *stack_pop(UndoGroup ***stack, int *len) {
    if (*len == 0)
        return NULL;
    return (*stack)[--(*len)];
}

static void stack_clear(UndoGroup ***stack, int *len) {
    while (*len > 0)
        group_free((*stack)[--(*len)]);
}

/* ===================================================================
 * Public init/free
 * =================================================================== */

void undo_state_init(UndoState *u) {
    if (!u)
        return;
    memset(u, 0, sizeof(*u));
}

void undo_state_free(UndoState *u) {
    if (!u)
        return;
    if (u->open) {
        group_free(u->open);
        u->open = NULL;
    }
    stack_clear(&u->undo, &u->undo_len);
    free(u->undo);
    u->undo = NULL;
    u->undo_cap = 0;
    stack_clear(&u->redo, &u->redo_len);
    free(u->redo);
    u->redo = NULL;
    u->redo_cap = 0;
}

/* ===================================================================
 * Group lifecycle
 * =================================================================== */

static void enforce_depth(UndoState *u) {
    while (u->undo_len > UNDO_MAX_DEPTH) {
        group_free(u->undo[0]);
        memmove(&u->undo[0], &u->undo[1],
                (size_t)(u->undo_len - 1) * sizeof(UndoGroup *));
        u->undo_len--;
    }
}

void undo_begin(struct Buffer *buf, const char *desc) {
    if (!buf)
        return;
    UndoState *u = &buf->undo;
    if (u->applying)
        return;
    if (u->open)
        undo_end(buf);
    u->open = calloc(1, sizeof(UndoGroup));
    if (!u->open)
        return;
    if (desc)
        strncpy(u->open->desc, desc, sizeof(u->open->desc) - 1);
}

void undo_end(struct Buffer *buf) {
    if (!buf)
        return;
    UndoState *u = &buf->undo;
    if (u->applying)
        return;
    if (!u->open)
        return;
    if (u->open->len == 0) {
        group_free(u->open);
        u->open = NULL;
        return;
    }
    stack_push(&u->undo, &u->undo_len, &u->undo_cap, u->open);
    u->open = NULL;
    enforce_depth(u);
    /* New edit invalidates redo history. */
    stack_clear(&u->redo, &u->redo_len);
}

int undo_has_open(const struct Buffer *buf) {
    return buf && buf->undo.open != NULL;
}

int undo_is_applying(const struct Buffer *buf) {
    return buf && buf->undo.applying;
}

/* ===================================================================
 * Recording
 * =================================================================== */

static UndoGroup *ensure_open(struct Buffer *buf) {
    UndoState *u = &buf->undo;
    if (u->applying)
        return NULL;
    if (!u->open) {
        u->open = calloc(1, sizeof(UndoGroup));
        if (!u->open)
            return NULL;
        strncpy(u->open->desc, "auto", sizeof(u->open->desc) - 1);
    }
    return u->open;
}

void undo_record_replace(struct Buffer *buf, int row_idx) {
    if (!buf)
        return;
    UndoGroup *g = ensure_open(buf);
    if (!g)
        return;
    if (row_idx < 0 || row_idx >= buf->num_rows)
        return;
    /* Coalesce: if the previous record is a REPLACE on the same row,
     * we already captured the original pre-mutation chars; nothing to do. */
    if (g->len > 0) {
        UndoRec *last = &g->recs[g->len - 1];
        if (last->kind == UR_REPLACE && last->row_idx == row_idx)
            return;
    }
    UndoRec *r = group_add_rec(g);
    if (!r)
        return;
    r->kind = UR_REPLACE;
    r->row_idx = row_idx;
    Row *row = &buf->rows[row_idx];
    r->data = sstr_new();
    sstr_append(&r->data, row->chars.data, row->chars.len);
}

void undo_record_insert(struct Buffer *buf, int row_idx, const char *data,
                        size_t len) {
    if (!buf)
        return;
    UndoGroup *g = ensure_open(buf);
    if (!g)
        return;
    UndoRec *r = group_add_rec(g);
    if (!r)
        return;
    r->kind = UR_INSERT;
    r->row_idx = row_idx;
    r->data = sstr_new();
    if (data && len)
        sstr_append(&r->data, data, len);
}

void undo_record_delete(struct Buffer *buf, int row_idx, const char *data,
                        size_t len) {
    if (!buf)
        return;
    UndoGroup *g = ensure_open(buf);
    if (!g)
        return;
    UndoRec *r = group_add_rec(g);
    if (!r)
        return;
    r->kind = UR_DELETE;
    r->row_idx = row_idx;
    r->data = sstr_new();
    if (data && len)
        sstr_append(&r->data, data, len);
}

/* ===================================================================
 * Apply
 * =================================================================== */

/* Apply a single record. dir = +1 means redo (forward), -1 means undo. */
static void apply_rec(struct Buffer *buf, UndoRec *r, int dir) {
    if (r->kind == UR_REPLACE) {
        if (r->row_idx < 0 || r->row_idx >= buf->num_rows)
            return;
        Row *row = &buf->rows[r->row_idx];
        SizedStr tmp = row->chars;
        row->chars = r->data;
        r->data = tmp;
        buf_row_update(row);
        buf->dirty++;
        return;
    }
    int doing_insert = (r->kind == UR_INSERT && dir > 0) ||
                       (r->kind == UR_DELETE && dir < 0);
    if (doing_insert) {
        buf_row_insert_in(buf, r->row_idx, r->data.data, r->data.len);
    } else {
        if (r->row_idx >= 0 && r->row_idx < buf->num_rows)
            buf_row_del_in(buf, r->row_idx);
    }
}

int undo_apply(struct Buffer *buf) {
    if (!buf)
        return 0;
    UndoState *u = &buf->undo;
    if (u->open)
        undo_end(buf);
    if (u->undo_len == 0)
        return 0;
    UndoGroup *g = stack_pop(&u->undo, &u->undo_len);
    if (!g)
        return 0;
    u->applying = 1;
    for (int i = g->len - 1; i >= 0; i--)
        apply_rec(buf, &g->recs[i], -1);
    u->applying = 0;
    stack_push(&u->redo, &u->redo_len, &u->redo_cap, g);
    return 1;
}

/* ===================================================================
 * Mode change hook: open insert group on entry, close on exit.
 * =================================================================== */

static void on_mode_change(const HookModeEvent *event) {
    if (!event)
        return;
    Buffer *buf = buf_cur();
    if (!buf)
        return;
    int entering_insert =
        (event->old_mode != MODE_INSERT && event->new_mode == MODE_INSERT);
    int leaving_insert =
        (event->old_mode == MODE_INSERT && event->new_mode != MODE_INSERT);
    if (entering_insert) {
        /* If an operator (e.g. `c`) already opened a group, let it span
         * the upcoming insert session instead of replacing it. Esc still
         * closes whichever group is open, so c<motion><text><Esc> stays
         * one undo step. */
        if (!undo_has_open(buf))
            undo_begin(buf, "insert");
    } else if (leaving_insert) {
        undo_end(buf);
    }
}

void undo_register_hooks(void) {
    hook_register_mode(HOOK_MODE_CHANGE, on_mode_change);
}

int redo_apply(struct Buffer *buf) {
    if (!buf)
        return 0;
    UndoState *u = &buf->undo;
    /* If the user made a fresh edit since the last undo, the open group
     * already cleared the redo stack via undo_end; nothing to do. */
    if (u->open)
        undo_end(buf);
    if (u->redo_len == 0)
        return 0;
    UndoGroup *g = stack_pop(&u->redo, &u->redo_len);
    if (!g)
        return 0;
    u->applying = 1;
    for (int i = 0; i < g->len; i++)
        apply_rec(buf, &g->recs[i], +1);
    u->applying = 0;
    /* Push back to undo stack directly (without going through undo_end,
     * which would clear the rest of the redo stack). */
    stack_push(&u->undo, &u->undo_len, &u->undo_cap, g);
    enforce_depth(u);
    return 1;
}
