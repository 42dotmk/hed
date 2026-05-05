/* multicursor: synchronized edits at multiple cursors.
 *
 * Adds extra cursors via :mc_add_below / :mc_add_above (or leader
 * keys). When extras exist, every keypress runs the full dispatch
 * once per cursor: the active first, then each extra (descending
 * (y,x) order so earlier positions don't shift under later edits).
 * Per-cursor dispatch restores the pre-keypress mode + key-sequence
 * state, so multi-key sequences (operators, leader bindings) and
 * mode changes (i/a/o/Esc) all replicate correctly.
 */

#include "hed.h"
#include "multicursor/multicursor.h"
#include <stdlib.h>

/* Re-entrancy guard: set while replaying so our own HOOK_KEYPRESS
 * doesn't recurse on the per-cursor dispatch. */
static int in_replay = 0;

/* Sort cursor pointers descending by (y, x). Editing later positions
 * first means earlier (unprocessed) positions don't shift. */
static int cursor_pos_cmp_desc(const void *a, const void *b) {
    const Cursor *ca = *(const Cursor *const *)a;
    const Cursor *cb = *(const Cursor *const *)b;
    if (ca->y != cb->y) return cb->y - ca->y;
    return cb->x - ca->x;
}

/* Run ed_dispatch_key(c) at every cursor. The active goes first;
 * each subsequent dispatch starts from the same pre-keypress state
 * (mode + key-sequence buffer), so e.g. `i` enters insert mode at
 * each cursor instead of typing 'i' as a literal at the second one. */
static void on_keypress(HookKeyEvent *event) {
    if (in_replay || !event) return;
    Buffer *buf = buf_cur();
    if (!buf || buf_cursor_count(buf) <= 1) return;

    int n = buf_cursor_count(buf);
    Cursor **all = malloc(sizeof(Cursor *) * (size_t)n);
    if (!all) return;
    for (int i = 0; i < n; i++) all[i] = buf->all_cursors[i];
    qsort(all, (size_t)n, sizeof(Cursor *), cursor_pos_cmp_desc);

    KeybindState saved_kb;
    keybind_state_save(&saved_kb);
    int saved_mode = E.mode;
    int c = event->key;

    Cursor *original_active = buf->cursor;

    in_replay = 1;
    for (int i = 0; i < n; i++) {
        /* The active cursor's count may have changed (e.g., the user
         * triggered :mc_clear which removes everything but active).
         * Bail out if the snapshot is no longer valid. */
        if ((int)arrlen(buf->all_cursors) < n) break;

        if (i > 0) {
            /* Restore pre-keypress dispatch state for this replay so
             * each cursor sees the same starting conditions. */
            keybind_state_load(&saved_kb);
            E.mode = saved_mode;
        }

        buf->cursor = all[i];
        Window *win = window_cur();
        if (win) {
            win->cursor.x = all[i]->x;
            win->cursor.y = all[i]->y;
        }

        ed_dispatch_key(c);

        if (win) {
            all[i]->x = win->cursor.x;
            all[i]->y = win->cursor.y;
        }
    }
    in_replay = 0;

    /* Restore the user's primary cursor as visually active. If the
     * active was destroyed by an mc_* command during dispatch, fall
     * back to whatever's at all_cursors[0]. */
    Cursor *restore_to = NULL;
    for (size_t i = 0; i < arrlen(buf->all_cursors); i++) {
        if (buf->all_cursors[i] == original_active) {
            restore_to = original_active;
            break;
        }
    }
    if (!restore_to && arrlen(buf->all_cursors) > 0)
        restore_to = buf->all_cursors[0];
    if (restore_to) {
        buf->cursor = restore_to;
        Window *win = window_cur();
        if (win) {
            win->cursor.x = restore_to->x;
            win->cursor.y = restore_to->y;
        }
    }

    free(all);
    event->consumed = 1;
}

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

    mapn(" md", kb_mc_add_below, "multicursor: add cursor below");
    mapn(" mu", kb_mc_add_above, "multicursor: add cursor above");
    mapn(" mc", kb_mc_clear,     "multicursor: clear extras");

    /* Single keypress hook handles every mode and every key — the
     * dispatch is replayed at each cursor with restored state. */
    hook_register_key(HOOK_KEYPRESS, on_keypress);

    return 0;
}

const Plugin plugin_multicursor = {
    .name   = "multicursor",
    .desc   = "synchronized edits at multiple cursors",
    .init   = multicursor_init,
    .deinit = NULL,
};
