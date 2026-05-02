/* multicursor: synchronized edits at multiple cursors.
 *
 * Adds extra cursors via :mc_add_below / :mc_add_above (or leader
 * keys). When the user types or backspaces in insert mode, the active
 * cursor's edit is mirrored at every extra cursor.
 *
 * The core auto-shifts non-active cursors as the buffer mutates
 * (see buf/buffer.c cursors_after_*). This plugin's job is just to
 * replay the same key at each extra so they all see the same edit.
 */

#include "hed.h"
#include "multicursor/multicursor.h"
#include <stdlib.h>

/* Re-entrancy guard: set while replaying so our own hook doesn't
 * recurse on the replayed edit's hook firing. */
static int in_replay = 0;

/* Sort cursor pointers descending by (y, x). Editing at later
 * positions first means earlier (unprocessed) positions don't shift,
 * so each replay lands at the cursor's intended column. */
static int cursor_pos_cmp_desc(const void *a, const void *b) {
    const Cursor *ca = *(const Cursor *const *)a;
    const Cursor *cb = *(const Cursor *const *)b;
    if (ca->y != cb->y) return cb->y - ca->y;
    return cb->x - ca->x;
}

typedef enum { MC_OP_INSERT_CHAR, MC_OP_DELETE_CHAR } McOp;

static void replay_at_extras(Buffer *buf, McOp op, int c) {
    Window *win = window_cur();
    if (!buf || !win || !buf->cursor) return;
    if (buf->all_cursors.len <= 1) return;

    Cursor *active = buf->cursor;
    int n_extras = (int)buf->all_cursors.len - 1;
    Cursor **extras = malloc(sizeof(Cursor *) * (size_t)n_extras);
    if (!extras) return;

    int j = 0;
    for (size_t i = 0; i < buf->all_cursors.len; i++) {
        if (buf->all_cursors.data[i] != active)
            extras[j++] = buf->all_cursors.data[i];
    }
    qsort(extras, (size_t)n_extras, sizeof(Cursor *), cursor_pos_cmp_desc);

    in_replay = 1;
    for (int i = 0; i < n_extras; i++) {
        buf->cursor = extras[i];
        win->cursor.x = extras[i]->x;
        win->cursor.y = extras[i]->y;
        switch (op) {
        case MC_OP_INSERT_CHAR: buf_insert_char_in(buf, c); break;
        case MC_OP_DELETE_CHAR: buf_del_char_in(buf); break;
        }
        /* sync handled by the edit primitive itself */
    }
    in_replay = 0;

    buf->cursor = active;
    win->cursor.x = active->x;
    win->cursor.y = active->y;
    free(extras);
}

static void on_char_insert(const HookCharEvent *e) {
    if (in_replay || !e || !e->buf) return;
    replay_at_extras(e->buf, MC_OP_INSERT_CHAR, e->c);
}

static void on_char_delete(const HookCharEvent *e) {
    if (in_replay || !e || !e->buf) return;
    replay_at_extras(e->buf, MC_OP_DELETE_CHAR, 0);
}

/* Clear extras when switching buffers — the per-buffer state stays,
 * but we don't want stale extras leaking across save/reload either.
 * For now keep them; users explicitly clear with :mc_clear. */

/* --- Commands --- */

static void cmd_mc_add_below(const char *args) {
    (void)args;
    BUFWIN(buf, win)
    if (!buf->cursor) { ed_set_status_message("multicursor: no cursor"); return; }
    int new_y = buf->cursor->y + 1;
    if (new_y >= buf->num_rows) {
        ed_set_status_message("multicursor: no row below");
        return;
    }
    int x = buf->cursor->x;
    int len = (int)buf->rows[new_y].chars.len;
    if (x > len) x = len;
    if (!buf_cursor_add(buf, new_y, x)) {
        ed_set_status_message("multicursor: out of memory");
        return;
    }
    ed_set_status_message("multicursor: %d cursors", buf_cursor_count(buf));
}

static void cmd_mc_add_above(const char *args) {
    (void)args;
    BUFWIN(buf, win)
    if (!buf->cursor) { ed_set_status_message("multicursor: no cursor"); return; }
    int new_y = buf->cursor->y - 1;
    if (new_y < 0) {
        ed_set_status_message("multicursor: no row above");
        return;
    }
    int x = buf->cursor->x;
    int len = (int)buf->rows[new_y].chars.len;
    if (x > len) x = len;
    if (!buf_cursor_add(buf, new_y, x)) {
        ed_set_status_message("multicursor: out of memory");
        return;
    }
    ed_set_status_message("multicursor: %d cursors", buf_cursor_count(buf));
}

static void cmd_mc_clear(const char *args) {
    (void)args;
    BUF(buf)
    buf_cursor_clear_extras(buf);
    ed_set_status_message("multicursor: cleared");
}

static void cmd_mc_count(const char *args) {
    (void)args;
    BUF(buf)
    ed_set_status_message("multicursor: %d cursors", buf_cursor_count(buf));
}

/* --- Keybind wrappers (mapn callbacks take void) --- */

static void kb_mc_add_below(void) { cmd_mc_add_below(NULL); }
static void kb_mc_add_above(void) { cmd_mc_add_above(NULL); }
static void kb_mc_clear(void)     { cmd_mc_clear(NULL); }

/* --- lifecycle --- */

static int multicursor_init(void) {
    cmd("mc_add_below", cmd_mc_add_below, "multicursor: add cursor below");
    cmd("mc_add_above", cmd_mc_add_above, "multicursor: add cursor above");
    cmd("mc_clear",     cmd_mc_clear,     "multicursor: keep only active cursor");
    cmd("mc_count",     cmd_mc_count,     "multicursor: show cursor count");

    /* Vim leader cluster. <space>md / <space>mu add cursors; <space>mc clears. */
    mapn(" md", kb_mc_add_below, "multicursor: add cursor below");
    mapn(" mu", kb_mc_add_above, "multicursor: add cursor above");
    mapn(" mc", kb_mc_clear,     "multicursor: clear extras");

    /* Replay character-level edits at every extra cursor. */
    hook_register_char(HOOK_CHAR_INSERT, MODE_INSERT, "*", on_char_insert);
    hook_register_char(HOOK_CHAR_DELETE, MODE_INSERT, "*", on_char_delete);

    return 0;
}

const Plugin plugin_multicursor = {
    .name   = "multicursor",
    .desc   = "synchronized edits at multiple cursors",
    .init   = multicursor_init,
    .deinit = NULL,
};
