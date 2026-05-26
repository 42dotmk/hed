/* multicursor: synchronized edits at multiple cursors.
 *
 * Add extra cursors with :mc_add_below/_above (or leader keys), or with
 * <C-n> in NORMAL/VISUAL to put a cursor at the next occurrence of the
 * word under cursor (or current single-line selection). Capital Q drops
 * the active cursor and advances to the next one in cyclic (y,x) order.
 *
 * When extras exist, every keypress runs the full per-mode dispatch
 * once per cursor — sorted descending by (y,x) so an edit at a later
 * position doesn't shift the column of an unprocessed cursor. The
 * pre-keypress mode + key-sequence state is restored between cursors so
 * multi-key sequences (operators, leaders) and mode changes (i/a/o/Esc)
 * replicate at every cursor instead of being half-consumed by the first.
 */

#include "hed.h"
#include "input/prompt.h"
#include "multicursor/multicursor.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* Re-entrancy guard: set while replaying so our own HOOK_KEYPRESS
 * doesn't recurse on the per-cursor dispatch. */
static int in_replay = 0;

/* :mc_debug toggles a verbose log_msg trace of every dispatch. */
static int mc_debug = 0;

static int cursor_pos_cmp_desc(const void *a, const void *b) {
    const Cursor *ca = *(const Cursor *const *)a;
    const Cursor *cb = *(const Cursor *const *)b;
    if (ca->y != cb->y) return cb->y - ca->y;
    return cb->x - ca->x;
}

static int cursor_pos_cmp_asc(const void *a, const void *b) {
    const Cursor *ca = *(const Cursor *const *)a;
    const Cursor *cb = *(const Cursor *const *)b;
    if (ca->y != cb->y) return ca->y - cb->y;
    return ca->x - cb->x;
}

/* Per-cursor replay of one keypress.
 *
 * Two-phase: first dispatch at the original active cursor (with
 * in_replay=0) so cursor-list operations like C-n / Q / mc_add_*
 * actually run exactly once. If that dispatch changed the cursor count,
 * it was a list operation and we stop — no replay at extras. Otherwise
 * we dispatch at the remaining cursors (with in_replay=1) so per-cursor
 * commands skip themselves but ordinary edits and motions replicate.
 *
 * Extras are sorted descending by (y, x) so an edit at a later position
 * doesn't shift the column of an unprocessed cursor. Each replay starts
 * from the same pre-keypress mode + key-sequence state so multi-key
 * sequences (operators, leaders) and mode changes (i/a/o/Esc) build up
 * independently per cursor. */
static void on_keypress(HookKeyEvent *event) {
    if (in_replay || !event) return;
    Buffer *buf = buf_cur();
    if (!buf || buf_cursor_count(buf) <= 1) return;

    /* Prompt input ( `:` / `/` / `?` ) is a single global line — every
     * dispatch appends to the same buffer, so replaying at N cursors
     * would type each char N times. Modals (dired/lsp/selectlist) also
     * route keys globally; replaying makes no sense there either. */
    if (prompt_active() || winmodal_current() != NULL) return;

    int n = buf_cursor_count(buf);
    Cursor *original_active = buf->cursor;

    Cursor **all = malloc(sizeof(Cursor *) * (size_t)n);
    if (!all) return;
    for (int i = 0; i < n; i++) all[i] = buf->all_cursors[i];

    KeybindState saved_kb;
    keybind_state_save(&saved_kb);
    int saved_mode = E.mode;
    int c = event->key;

    if (mc_debug)
        log_msg("mc: keypress c=%d (0x%x) n=%d mode=%d",
                c, c, n, saved_mode);

    /* Phase 1: dispatch once at the active cursor, in_replay = 0.
     * Multicursor's own commands (C-n, Q, leader-md/mu/mc) run here. */
    Window *win = window_cur();
    if (win && original_active) {
        win->cursor.x = original_active->x;
        win->cursor.y = original_active->y;
    }
    /* buf->cursor is already original_active. */

    if (mc_debug && original_active)
        log_msg("mc:   active before (%d,%d) mode=%d",
                original_active->y, original_active->x, E.mode);

    ed_dispatch_key(c);

    /* The active cursor may have been removed (Q) or replaced — e.g.
     * mc_skip points buf->cursor at the next cursor and removes the
     * old one; C-n points buf->cursor at the newly-added cursor.
     *
     * Only mirror win→original_active when the dispatch left
     * buf->cursor pointing at original_active. Otherwise win->cursor
     * holds the *new* active's position and copying that into
     * original_active would clobber its real location (which is what
     * caused C-n to move the previously-active cursor onto the new
     * match, leaving two cursors stacked there). */
    int active_still_in_list = 0;
    for (ptrdiff_t i = 0; i < arrlen(buf->all_cursors); i++) {
        if (buf->all_cursors[i] == original_active) {
            active_still_in_list = 1;
            break;
        }
    }
    if (active_still_in_list && buf->cursor == original_active && win) {
        original_active->x = win->cursor.x;
        original_active->y = win->cursor.y;
    }

    if (mc_debug)
        log_msg("mc:   after active, arrlen=%d",
                (int)arrlen(buf->all_cursors));

    /* A cursor-list operation happened (C-n, Q, mc_add_*, mc_clear) —
     * exactly one dispatch is the right amount. Don't replay. Leave
     * buf->cursor wherever the operation pointed it. */
    if ((int)arrlen(buf->all_cursors) != n) {
        free(all);
        event->consumed = 1;
        return;
    }

    /* Phase 2: replay at every cursor that wasn't the original active.
     * Sort the remaining set descending by (y, x) so an edit at a later
     * position can't shift the column of an unprocessed cursor. */
    qsort(all, (size_t)n, sizeof(Cursor *), cursor_pos_cmp_desc);

    in_replay = 1;
    for (int i = 0; i < n; i++) {
        if ((int)arrlen(buf->all_cursors) < n) break;
        if (prompt_active() || winmodal_current() != NULL) break;
        if (all[i] == original_active) continue;

        keybind_state_load(&saved_kb);
        E.mode = saved_mode;

        buf->cursor = all[i];
        Window *w = window_cur();
        if (w) {
            w->cursor.x = all[i]->x;
            w->cursor.y = all[i]->y;
        }

        if (mc_debug)
            log_msg("mc:   extra %d before (%d,%d) mode=%d",
                    i, all[i]->y, all[i]->x, E.mode);

        ed_dispatch_key(c);

        if (w) {
            all[i]->x = w->cursor.x;
            all[i]->y = w->cursor.y;
        }

        if (mc_debug)
            log_msg("mc:   extra %d after  (%d,%d) mode=%d",
                    i, all[i]->y, all[i]->x, E.mode);
    }
    in_replay = 0;

    /* Restore the user's primary cursor as visually active. */
    if (active_still_in_list) {
        buf->cursor = original_active;
        Window *w = window_cur();
        if (w) {
            w->cursor.x = original_active->x;
            w->cursor.y = original_active->y;
        }
    } else if (arrlen(buf->all_cursors) > 0) {
        buf->cursor = buf->all_cursors[0];
        Window *w = window_cur();
        if (w) {
            w->cursor.x = buf->all_cursors[0]->x;
            w->cursor.y = buf->all_cursors[0]->y;
        }
    }

    free(all);
    event->consumed = 1;
}

/* --- next-match search --- */

/* Case-sensitive substring search starting at (start_y, start_x),
 * wrapping past the last row. Fills *out_y / *out_x and returns 1 on
 * match; returns 0 if the query isn't found anywhere. */
static int mc_find_next(Buffer *buf, const char *q, size_t qlen,
                        int start_y, int start_x,
                        int *out_y, int *out_x) {
    if (!buf || !q || qlen == 0 || buf->num_rows == 0) return 0;
    int rows = buf->num_rows;

    for (int i = 0; i <= rows; i++) {
        int y = (start_y + i) % rows;
        Row *row = &buf->rows[y];
        int len = (int)row->chars.len;
        int from = (i == 0) ? start_x : 0;
        if (from < 0) from = 0;
        for (int x = from; x + (int)qlen <= len; x++) {
            if (memcmp(row->chars.data + x, q, qlen) == 0) {
                *out_y = y;
                *out_x = x;
                return 1;
            }
        }
    }
    return 0;
}

/* Mirror of mc_find_next walking backward. On the start row the match
 * must end at or before start_x (i.e. match-start <= start_x - qlen);
 * wrapped rows are scanned from the rightmost candidate position. */
static int mc_find_prev(Buffer *buf, const char *q, size_t qlen,
                        int start_y, int start_x,
                        int *out_y, int *out_x) {
    if (!buf || !q || qlen == 0 || buf->num_rows == 0) return 0;
    int rows = buf->num_rows;

    for (int i = 0; i <= rows; i++) {
        int y = ((start_y - i) % rows + rows) % rows;
        Row *row = &buf->rows[y];
        int len = (int)row->chars.len;
        int upper = (i == 0) ? (start_x - (int)qlen)
                             : (len - (int)qlen);
        if (upper > len - (int)qlen) upper = len - (int)qlen;
        for (int x = upper; x >= 0; x--) {
            if (memcmp(row->chars.data + x, q, qlen) == 0) {
                *out_y = y;
                *out_x = x;
                return 1;
            }
        }
    }
    return 0;
}

/* C-n: add a cursor at the next occurrence of the word under cursor
 * (NORMAL) or the active visual selection (VISUAL, single-line only).
 * Skipped during replay — adding a cursor is a global action, not a
 * per-cursor edit, so we want exactly one new cursor per keypress. */
static void kb_mc_add_next_match(void) {
    if (in_replay) return;
    BUFWIN(buf, win)
    if (buf->num_rows == 0) return;

    SizedStr q = {0};
    int from_y = win->cursor.y;
    int from_x = win->cursor.x + 1;

    if (E.mode == MODE_VISUAL) {
        int ay = win->sel.anchor_y, ax = win->sel.anchor_x;
        int cy = win->sel.cursor_y, cx = win->sel.cursor_x;
        if (ay > cy || (ay == cy && ax > cx)) {
            int ty = ay, tx = ax; ay = cy; ax = cx; cy = ty; cx = tx;
        }
        if (ay != cy) {
            ed_set_status_message("multicursor: multi-line selection not supported");
            return;
        }
        Row *row = &buf->rows[ay];
        if (ax < 0) ax = 0;
        if (cx > (int)row->chars.len - 1) cx = (int)row->chars.len - 1;
        int sel_end = cx + 1; /* visual selection is inclusive on the cursor side */
        if (sel_end > (int)row->chars.len) sel_end = (int)row->chars.len;
        if (sel_end <= ax) {
            ed_set_status_message("multicursor: empty selection");
            return;
        }
        q = sstr_from(row->chars.data + ax, (size_t)(sel_end - ax));
        from_y = ay;
        from_x = sel_end;
    } else {
        if (!buf_get_word_under_cursor(&q) || q.len == 0) {
            ed_set_status_message("multicursor: no word under cursor");
            sstr_free(&q);
            return;
        }
        /* Start the search after the end of the current word so the
         * same word at the cursor isn't matched again. */
        Row *row = &buf->rows[win->cursor.y];
        int x = win->cursor.x;
        while (x < (int)row->chars.len &&
               (isalnum((unsigned char)row->chars.data[x]) ||
                row->chars.data[x] == '_'))
            x++;
        from_x = x;
    }

    /* Avoid creating a duplicate cursor at a position that already has
     * one — instead, just move active there and report. */
    int my, mx;
    if (!mc_find_next(buf, q.data, q.len, from_y, from_x, &my, &mx)) {
        ed_set_status_message("multicursor: no more matches");
        sstr_free(&q);
        return;
    }

    /* Mirror the term into E.search_query so `n` and `:ssearch` reuse
     * the same word picker semantics as `*` would. */
    sstr_free(&E.search_query);
    E.search_query = sstr_from(q.data, q.len);
    E.search_is_regex = 0;
    sstr_free(&q);

    /* If a cursor already lives at (my, mx), just make it active. */
    for (ptrdiff_t i = 0; i < arrlen(buf->all_cursors); i++) {
        Cursor *c = buf->all_cursors[i];
        if (c->y == my && c->x == mx) {
            buf->cursor = c;
            win->cursor.y = my;
            win->cursor.x = mx;
            ed_set_status_message("multicursor: %d cursors (moved to existing match)",
                                  buf_cursor_count(buf));
            return;
        }
    }

    Cursor *nc = buf_cursor_add(buf, my, mx);
    if (!nc) {
        ed_set_status_message("multicursor: out of memory");
        return;
    }
    buf->cursor = nc;
    win->cursor.y = my;
    win->cursor.x = mx;
    /* C-n is most useful from NORMAL — leave VISUAL mode alone so the
     * caller's selection visualization stays, but drop the selection
     * if we were in VISUAL so subsequent edits are positional. */
    if (E.mode == MODE_VISUAL) {
        win->sel.type = SEL_NONE;
        ed_set_mode(MODE_NORMAL);
    }
    ed_set_status_message("multicursor: %d cursors", buf_cursor_count(buf));
}

/* Mirror of kb_mc_add_next_match walking backward — leaves a cursor at
 * the current occurrence and advances the active cursor to the previous
 * match. */
static void kb_mc_add_prev_match(void) {
    if (in_replay) return;
    BUFWIN(buf, win)
    if (buf->num_rows == 0) return;

    SizedStr q = {0};
    int from_y = win->cursor.y;
    int from_x = win->cursor.x;

    if (E.mode == MODE_VISUAL) {
        int ay = win->sel.anchor_y, ax = win->sel.anchor_x;
        int cy = win->sel.cursor_y, cx = win->sel.cursor_x;
        if (ay > cy || (ay == cy && ax > cx)) {
            int ty = ay, tx = ax; ay = cy; ax = cx; cy = ty; cx = tx;
        }
        if (ay != cy) {
            ed_set_status_message("multicursor: multi-line selection not supported");
            return;
        }
        Row *row = &buf->rows[ay];
        if (ax < 0) ax = 0;
        if (cx > (int)row->chars.len - 1) cx = (int)row->chars.len - 1;
        int sel_end = cx + 1;
        if (sel_end > (int)row->chars.len) sel_end = (int)row->chars.len;
        if (sel_end <= ax) {
            ed_set_status_message("multicursor: empty selection");
            return;
        }
        q = sstr_from(row->chars.data + ax, (size_t)(sel_end - ax));
        from_y = ay;
        from_x = ax;
    } else {
        if (!buf_get_word_under_cursor(&q) || q.len == 0) {
            ed_set_status_message("multicursor: no word under cursor");
            sstr_free(&q);
            return;
        }
        /* Walk back to the start of the current word so we look for an
         * earlier occurrence, not this one again. */
        Row *row = &buf->rows[win->cursor.y];
        int x = win->cursor.x;
        while (x > 0 &&
               (isalnum((unsigned char)row->chars.data[x-1]) ||
                row->chars.data[x-1] == '_'))
            x--;
        from_x = x;
    }

    int my, mx;
    if (!mc_find_prev(buf, q.data, q.len, from_y, from_x, &my, &mx)) {
        ed_set_status_message("multicursor: no more matches");
        sstr_free(&q);
        return;
    }

    sstr_free(&E.search_query);
    E.search_query = sstr_from(q.data, q.len);
    E.search_is_regex = 0;
    sstr_free(&q);

    for (ptrdiff_t i = 0; i < arrlen(buf->all_cursors); i++) {
        Cursor *c = buf->all_cursors[i];
        if (c->y == my && c->x == mx) {
            buf->cursor = c;
            win->cursor.y = my;
            win->cursor.x = mx;
            ed_set_status_message("multicursor: %d cursors (moved to existing match)",
                                  buf_cursor_count(buf));
            return;
        }
    }

    Cursor *nc = buf_cursor_add(buf, my, mx);
    if (!nc) {
        ed_set_status_message("multicursor: out of memory");
        return;
    }
    buf->cursor = nc;
    win->cursor.y = my;
    win->cursor.x = mx;
    if (E.mode == MODE_VISUAL) {
        win->sel.type = SEL_NONE;
        ed_set_mode(MODE_NORMAL);
    }
    ed_set_status_message("multicursor: %d cursors", buf_cursor_count(buf));
}

/* Q: drop the active cursor; activate the next in cyclic (y,x) order.
 * Skipped during replay — Q is a "cursor list" operation, not an edit. */
static void kb_mc_skip(void) {
    if (in_replay) return;
    BUFWIN(buf, win)
    int n = buf_cursor_count(buf);
    if (n <= 1) {
        ed_set_status_message("multicursor: only one cursor");
        return;
    }
    Cursor *active = buf->cursor;
    if (!active) return;

    Cursor **sorted = malloc(sizeof(Cursor *) * (size_t)n);
    if (!sorted) return;
    for (int i = 0; i < n; i++) sorted[i] = buf->all_cursors[i];
    qsort(sorted, (size_t)n, sizeof(Cursor *), cursor_pos_cmp_asc);

    int idx = -1;
    for (int i = 0; i < n; i++) {
        if (sorted[i] == active) { idx = i; break; }
    }
    if (idx < 0) { free(sorted); return; }

    Cursor *next = sorted[(idx + 1) % n];
    free(sorted);

    /* Pivot the primary first — buf_cursor_remove() refuses to drop the
     * active cursor, so the order matters. */
    buf->cursor = next;
    win->cursor.x = next->x;
    win->cursor.y = next->y;

    buf_cursor_remove(buf, active);
    ed_set_status_message("multicursor: %d cursors", buf_cursor_count(buf));
}

/* --- Commands --- */

/* Plant a stationary cursor at the current position and advance the
 * active cursor by one row in `dy`. Net effect: repeated C-Down "drops
 * a trail" of cursors as you descend (and C-Up as you ascend), which
 * mirrors how C-n leaves a cursor behind at each previous match. */
static void mc_add_and_advance(int dy) {
    if (in_replay) return;
    BUFWIN(buf, win)
    if (!buf->cursor) { ed_set_status_message("multicursor: no cursor"); return; }
    int new_y = buf->cursor->y + dy;
    if (new_y < 0 || new_y >= buf->num_rows) {
        ed_set_status_message("multicursor: no row %s",
                              dy > 0 ? "below" : "above");
        return;
    }
    int x = buf->cursor->x;
    int len = (int)buf->rows[new_y].chars.len;
    if (x > len) x = len;
    Cursor *nc = buf_cursor_add(buf, new_y, x);
    if (!nc) {
        ed_set_status_message("multicursor: out of memory");
        return;
    }
    buf->cursor = nc;
    win->cursor.y = new_y;
    win->cursor.x = x;
    ed_set_status_message("multicursor: %d cursors", buf_cursor_count(buf));
}

static void cmd_mc_add_below(const char *args) {
    (void)args;
    mc_add_and_advance(+1);
}

static void cmd_mc_add_above(const char *args) {
    (void)args;
    mc_add_and_advance(-1);
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
    int n = buf_cursor_count(buf);
    ed_set_status_message("multicursor: %d cursors", n);
    if (mc_debug) {
        for (ptrdiff_t i = 0; i < arrlen(buf->all_cursors); i++) {
            Cursor *c = buf->all_cursors[i];
            log_msg("mc:   cursor[%td] at (%d,%d)%s", i, c->y, c->x,
                    c == buf->cursor ? " <- active" : "");
        }
    }
}

static void cmd_mc_next_match(const char *args) {
    (void)args;
    kb_mc_add_next_match();
}

static void cmd_mc_prev_match(const char *args) {
    (void)args;
    kb_mc_add_prev_match();
}

static void cmd_mc_skip(const char *args) {
    (void)args;
    kb_mc_skip();
}

static void cmd_mc_debug(const char *args) {
    if (!args || !*args) {
        mc_debug = !mc_debug;
    } else if (strcmp(args, "on") == 0) {
        mc_debug = 1;
    } else if (strcmp(args, "off") == 0) {
        mc_debug = 0;
    } else {
        mc_debug = !mc_debug;
    }
    ed_set_status_message("multicursor: debug %s", mc_debug ? "on" : "off");
}

/* --- Keybind wrappers (mapn callbacks take void) --- */

static void kb_mc_add_below(void) { cmd_mc_add_below(NULL); }
static void kb_mc_add_above(void) { cmd_mc_add_above(NULL); }
static void kb_mc_clear(void)     { cmd_mc_clear(NULL); }

/* --- lifecycle --- */

static int multicursor_init(void) {
    cmd("mc_add_below",  cmd_mc_add_below,  "multicursor: add cursor below");
    cmd("mc_add_above",  cmd_mc_add_above,  "multicursor: add cursor above");
    cmd("mc_clear",      cmd_mc_clear,      "multicursor: keep only active cursor");
    cmd("mc_count",      cmd_mc_count,      "multicursor: show cursor count");
    cmd("mc_next_match", cmd_mc_next_match, "multicursor: add cursor at next match");
    cmd("mc_prev_match", cmd_mc_prev_match, "multicursor: add cursor at previous match");
    cmd("mc_skip",       cmd_mc_skip,       "multicursor: skip current cursor");
    cmd("mc_debug",      cmd_mc_debug,      "multicursor: toggle dispatch logging");

    mapn(" md", kb_mc_add_below, "multicursor: add cursor below");
    mapn(" mu", kb_mc_add_above, "multicursor: add cursor above");
    mapn(" mc", kb_mc_clear,     "multicursor: clear extras");

    mapn("<C-Down>", kb_mc_add_below, "multicursor: add cursor below");
    mapn("<C-Up>",   kb_mc_add_above, "multicursor: add cursor above");
    mapi("<C-Down>", kb_mc_add_below, "multicursor: add cursor below");
    mapi("<C-Up>",   kb_mc_add_above, "multicursor: add cursor above");

    mapn("<C-n>", kb_mc_add_next_match, "multicursor: cursor at next match");
    mapv("<C-n>", kb_mc_add_next_match, "multicursor: cursor at next match");
    mapn(" mn",   kb_mc_add_next_match, "multicursor: cursor at next match");
    mapn(" mp",   kb_mc_add_prev_match, "multicursor: cursor at previous match");
    mapn("Q",     kb_mc_skip,           "multicursor: skip cursor / cycle to next");

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
