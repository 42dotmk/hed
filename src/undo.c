#include "hed.h"

/* In-memory stacks for undo/redo */
typedef struct {
    UndoRec *items;
    int len;
    int cap;
} UndoVec;

static UndoVec UNDO = {0}, REDO = {0};
static int current_group_id = 0;
static int group_open = 0;
static int insert_group_open = 0;
static int applying = 0;
static size_t cap_bytes = 4 * 1024 * 1024; /* default 4MB */
static size_t used_bytes = 0; /* sum of payload sizes in UNDO only */

extern Ed E;

static void s_free(SizedStr *s) { sstr_free(s); }
static void rec_free(UndoRec *r) { if (r) s_free(&r->payload); }

static void vec_reserve(UndoVec *v, int need) {
    if (v->cap >= need) return;
    int ncap = v->cap ? v->cap * 2 : 64;
    if (ncap < need) ncap = need;
    v->items = realloc(v->items, (size_t)ncap * sizeof(UndoRec));
    v->cap = ncap;
}

static void vec_push(UndoVec *v, const UndoRec *r) {
    vec_reserve(v, v->len + 1);
    v->items[v->len++] = *r; /* shallow copy; payload already allocated */
}

static int vec_pop_group(UndoVec *v, int group_id) {
    /* Returns number of records popped for this group (from end). */
    int count = 0;
    while (v->len > 0) {
        UndoRec *r = &v->items[v->len - 1];
        if (r->group_id != group_id) break;
        v->len--;
        count++;
    }
    return count;
}

static void prune_undo_cap(void) {
    /* Drop oldest group(s) until within cap_bytes. */
    if (used_bytes <= cap_bytes) return;
    if (UNDO.len == 0) return;
    /* Find the first group's range */
    int first_gid = UNDO.items[0].group_id;
    int i = 0;
    while (i < UNDO.len && UNDO.items[i].group_id == first_gid) {
        used_bytes -= UNDO.items[i].payload.len;
        rec_free(&UNDO.items[i]);
        i++;
    }
    /* Shift remaining */
    if (i > 0) {
        memmove(&UNDO.items[0], &UNDO.items[i], (size_t)(UNDO.len - i) * sizeof(UndoRec));
        UNDO.len -= i;
    }
}

void undo_init(void) {
    UNDO.items = NULL; UNDO.len = UNDO.cap = 0;
    REDO.items = NULL; REDO.len = REDO.cap = 0;
    current_group_id = 0; group_open = 0; insert_group_open = 0;
    applying = 0; used_bytes = 0; cap_bytes = 4 * 1024 * 1024;
}

void undo_set_cap(size_t bytes) { cap_bytes = bytes; }

void undo_begin_group(void) {
    if (!group_open) {
        group_open = 1;
        current_group_id++;
    }
}

void undo_commit_group(void) { group_open = 0; }

void undo_open_insert_group(void) {
    if (!insert_group_open) {
        undo_begin_group();
        insert_group_open = 1;
    }
}

void undo_close_insert_group(void) {
    if (insert_group_open) {
        undo_commit_group();
        insert_group_open = 0;
    }
}

void undo_on_mode_change(EditorMode old_mode, EditorMode new_mode) {
    if (old_mode == MODE_INSERT && new_mode != MODE_INSERT) {
        undo_close_insert_group();
    }
}

void undo_clear_redo(void) {
    /* Free all redo records */
    for (int i = 0; i < REDO.len; i++) rec_free(&REDO.items[i]);
    REDO.len = 0;
}

int undo_is_applying(void) { return applying; }

/* ---------- Buffer editing primitives for apply ---------- */

static void buf_insert_text_at(int y, int x, const char *data, size_t len) {
    Buffer *buf = buf_cur(); if (!buf) return;
    if (buf->num_rows == 0) {
        buf_row_insert(0, "", 0);
    }
    if (y < 0) y = 0; if (y > buf->num_rows) y = buf->num_rows;
    if (y == buf->num_rows) {
        /* append new line if inserting beyond last line and no current line */
        buf_row_insert(buf->num_rows, "", 0);
        y = buf->num_rows - 1; x = 0;
    }
    Row *row = &buf->rows[y];
    if (x < 0) x = 0; if (x > (int)row->chars.len) x = (int)row->chars.len;

    /* Insert segment by segment, splitting on '\n' */
    int cx = x;
    size_t i = 0; size_t start = 0;
    while (i <= len) {
        if (i == len || data[i] == '\n') {
            size_t seglen = i - start;
            for (size_t k = 0; k < seglen; k++) {
                buf_row_insert_char(&buf->rows[y], cx + (int)k, data[start + k]);
            }
            cx += (int)seglen;
            if (i < len && data[i] == '\n') {
                /* split line at cx */
                Row *r = &buf->rows[y];
                const char *rest = r->chars.data + cx;
                size_t rest_len = r->chars.len - cx;
                buf_row_insert(y + 1, rest, rest_len);
                /* truncate current line to cx */
                r = &buf->rows[y];
                r->chars.len = cx;
                r->chars.data[r->chars.len] = '\0';
                buf_row_update(r);
                y += 1; cx = 0;
            }
            start = i + 1;
        }
        i++;
    }
}

static void buf_delete_len_at(int y, int x, size_t len) {
    Buffer *buf = buf_cur(); if (!buf) return;
    if (len == 0) return;
    if (y < 0) y = 0; if (y >= buf->num_rows) return;
    if (x < 0) x = 0;

    while (len > 0 && y < buf->num_rows) {
        Row *row = &buf->rows[y];
        if (x < (int)row->chars.len) {
            /* delete char at (y,x) */
            buf_row_del_char(row, x);
            len--;
        } else {
            /* at end of line: delete newline by merging next line */
            if (y + 1 >= buf->num_rows) break;
            Row *next = &buf->rows[y + 1];
            buf_row_append(row, &next->chars);
            buf_row_del(y + 1);
            len--; /* newline consumed */
        }
    }
}

/* ---------- Public push helpers ---------- */

/* no push_common to avoid unused warnings */

void undo_push_insert(int y, int x, const char *data, size_t len,
                      int cy_before, int cx_before, int cy_after, int cx_after) {
    UndoRec r; r.type = UREC_INSERT_TEXT; r.y = y; r.x = x;
    r.payload = sstr_from(data ? data : "", len);
    r.cy_before = cy_before; r.cx_before = cx_before;
    r.cy_after = cy_after; r.cx_after = cx_after;
    if (!group_open) { undo_begin_group(); }
    r.group_id = current_group_id;
    vec_push(&UNDO, &r);
    used_bytes += r.payload.len;
    undo_clear_redo();
    prune_undo_cap();
}

void undo_push_delete(int y, int x, const char *data, size_t len,
                      int cy_before, int cx_before, int cy_after, int cx_after) {
    UndoRec r; r.type = UREC_DELETE_TEXT; r.y = y; r.x = x;
    r.payload = sstr_from(data ? data : "", len);
    r.cy_before = cy_before; r.cx_before = cx_before;
    r.cy_after = cy_after; r.cx_after = cx_after;
    if (!group_open) { undo_begin_group(); }
    r.group_id = current_group_id;
    vec_push(&UNDO, &r);
    used_bytes += r.payload.len;
    undo_clear_redo();
    prune_undo_cap();
}

/* ---------- Apply (undo/redo) ---------- */

static void apply_forward(const UndoRec *r) {
    switch (r->type) {
        case UREC_INSERT_TEXT:
            buf_insert_text_at(r->y, r->x, r->payload.data, r->payload.len);
            { Buffer *b = buf_cur(); if (b) { b->cursor_y = r->cy_after; b->cursor_x = r->cx_after; } }
            break;
        case UREC_DELETE_TEXT:
            buf_delete_len_at(r->y, r->x, r->payload.len);
            { Buffer *b = buf_cur(); if (b) { b->cursor_y = r->cy_after; b->cursor_x = r->cx_after; } }
            break;
    }
}

static void apply_inverse(const UndoRec *r) {
    switch (r->type) {
        case UREC_INSERT_TEXT:
            buf_delete_len_at(r->y, r->x, r->payload.len);
            { Buffer *b = buf_cur(); if (b) { b->cursor_y = r->cy_before; b->cursor_x = r->cx_before; } }
            break;
        case UREC_DELETE_TEXT:
            buf_insert_text_at(r->y, r->x, r->payload.data, r->payload.len);
            { Buffer *b = buf_cur(); if (b) { b->cursor_y = r->cy_before; b->cursor_x = r->cx_before; } }
            break;
    }
}

int undo_perform(void) {
    if (UNDO.len == 0) return 0;
    int gid = UNDO.items[UNDO.len - 1].group_id;
    applying = 1;
    /* collect records of this group */
    int start = UNDO.len - 1;
    while (start >= 0 && UNDO.items[start].group_id == gid) start--;
    start++;
    /* apply in reverse order, and move to REDO */
    for (int i = UNDO.len - 1; i >= start; i--) {
        UndoRec *r = &UNDO.items[i];
        apply_inverse(r);
    }
    /* move group to REDO */
    for (int i = start; i < UNDO.len; i++) {
        vec_push(&REDO, &UNDO.items[i]);
        used_bytes -= UNDO.items[i].payload.len;
    }
    UNDO.len = start;
    applying = 0;
    return 1;
}

int redo_perform(void) {
    if (REDO.len == 0) return 0;
    int gid = REDO.items[REDO.len - 1].group_id;
    applying = 1;
    /* find group's start in REDO */
    int start = REDO.len - 1;
    while (start >= 0 && REDO.items[start].group_id == gid) start--;
    start++;
    /* apply in forward order */
    for (int i = start; i < REDO.len; i++) {
        UndoRec *r = &REDO.items[i];
        apply_forward(r);
    }
    /* move to UNDO */
    for (int i = start; i < REDO.len; i++) {
        vec_push(&UNDO, &REDO.items[i]);
        used_bytes += REDO.items[i].payload.len;
    }
    REDO.len = start;
    applying = 0;
    prune_undo_cap();
    return 1;
}
