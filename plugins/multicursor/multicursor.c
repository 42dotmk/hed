/* multicursor: extra cursors with optional synchronized edits.
 *
 * Add extra cursors with :mc_add_below/_above (or leader keys), or with
 * <C-n> in NORMAL/VISUAL to put a cursor at the next occurrence of the
 * word under cursor (or current single-line selection). <M-n> / '* puts
 * a cursor at every occurrence in the buffer at once. Capital Q drops
 * the active cursor and advances to the next one in cyclic (y,x) order.
 * ` mj`/` mk` (:mc_jump_next/:mc_jump_prev) cycle the active cursor
 * through the set without dropping any.
 *
 * Extra cursors are passive markers by default: you move and edit with
 * the active cursor only, while the others hold their positions (core
 * auto-shifts them when edits land before them). Turning sync on
 * (:mc_sync, ` ms`) enables synchronized editing: every keypress runs
 * the full per-mode dispatch once per cursor — sorted descending by
 * (y,x) so an edit at a later position doesn't shift the column of an
 * unprocessed cursor. The pre-keypress mode + key-sequence state is
 * restored between cursors so multi-key sequences (operators, leaders)
 * and mode changes (i/a/o/Esc) replicate at every cursor instead of
 * being half-consumed by the first.
 *
 * Cursor sets are per (buffer, window) pair — core parks and restores
 * them as focus moves (see buf_cursors_bind_window). */

#include "hed.h"
#include "input/prompt.h"
#include "multicursor/multicursor.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* Re-entrancy guard: set while replaying so our own HOOK_KEYPRESS
 * doesn't recurse on the per-cursor dispatch. */
static int in_replay = 0;

/* Synchronized-edit switch (:mc_sync / ` ms`). Off by default: extra
 * cursors are passive markers and every keypress applies to the active
 * cursor only. On: keypresses replay at every cursor. */
static int mc_sync = 0;

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
    /* Sync off: extras are passive markers; the key dispatches normally
     * at the active cursor only. */
    if (!mc_sync) return;
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
        log_msg("mc: keypress c=%d (0x%x) n=%d mode=%d", c, c, n, saved_mode);

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

    /* Capture any follow-on keys the handler reads synchronously via
     * ed_read_key() (operator text objects like `diw`/`ciw`, `r`<char>,
     * `f`/`t`<char>) so the identical sequence can be replayed at each
     * extra cursor instead of blocking on the terminal there. */
    ed_key_capture_begin();
    ed_dispatch_key(c);
    const int *follow_keys = NULL;
    int follow_n = ed_key_capture_end(&follow_keys);
    int follow[64];
    if (follow_n > (int)(sizeof(follow) / sizeof(follow[0])))
        follow_n = (int)(sizeof(follow) / sizeof(follow[0]));
    for (int i = 0; i < follow_n; i++) follow[i] = follow_keys[i];

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

    /* The dispatch may have turned sync off (` ms` / :mc_sync) — stop
     * here instead of replaying the toggle at every cursor, which
     * would flip it right back. */
    if (!mc_sync) {
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

        /* Replay the same follow-on keys the active cursor consumed so a
         * multi-key handler (operator + text object, r/f/t) completes here
         * too rather than blocking on terminal input. */
        if (follow_n > 0) ed_key_replay_begin(follow, follow_n);
        ed_dispatch_key(c);
        if (follow_n > 0) ed_key_replay_finish();

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

typedef struct { int y, x; } McPos;

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

/* Extract the search query at the active cursor: the single-line
 * visual selection in VISUAL mode, the word under cursor in NORMAL.
 * On success fills *q (caller frees) plus the query's location in the
 * buffer — (*sy, *sx) is the start, *ex one past the end — and
 * returns 1. Returns 0 with a status message set otherwise. */
static int mc_query_at_cursor(Buffer *buf, Window *win, SizedStr *q,
                              int *sy, int *sx, int *ex) {
    if (E.mode == MODE_VISUAL) {
        /* The live end of the selection is win->cursor — sel.cursor_*
         * is not updated by motions, only the anchor is real. */
        int ay = win->sel.anchor_y, ax = win->sel.anchor_x;
        int cy = win->cursor.y, cx = win->cursor.x;
        if (ay > cy || (ay == cy && ax > cx)) {
            int ty = ay, tx = ax; ay = cy; ax = cx; cy = ty; cx = tx;
        }
        if (ay != cy) {
            ed_set_status_message("multicursor: multi-line selection not supported");
            return 0;
        }
        Row *row = &buf->rows[ay];
        if (ax < 0) ax = 0;
        if (cx > (int)row->chars.len - 1) cx = (int)row->chars.len - 1;
        int sel_end = cx + 1; /* visual selection is inclusive on the cursor side */
        if (sel_end > (int)row->chars.len) sel_end = (int)row->chars.len;
        if (sel_end <= ax) {
            ed_set_status_message("multicursor: empty selection");
            return 0;
        }
        *q = sstr_from(row->chars.data + ax, (size_t)(sel_end - ax));
        *sy = ay;
        *sx = ax;
        *ex = sel_end;
        return 1;
    }

    if (!buf_get_word_under_cursor(q) || q->len == 0) {
        ed_set_status_message("multicursor: no word under cursor");
        sstr_free(q);
        return 0;
    }
    Row *row = &buf->rows[win->cursor.y];
    int b = win->cursor.x, e = win->cursor.x;
    while (b > 0 &&
           (isalnum((unsigned char)row->chars.data[b-1]) ||
            row->chars.data[b-1] == '_'))
        b--;
    while (e < (int)row->chars.len &&
           (isalnum((unsigned char)row->chars.data[e]) ||
            row->chars.data[e] == '_'))
        e++;
    *sy = win->cursor.y;
    *sx = b;
    *ex = e;
    return 1;
}

/* Mirror the term into E.search_query so `n` and `:ssearch` reuse
 * the same word picker semantics as `*` would. */
static void mc_seed_search(const SizedStr *q) {
    sstr_free(&E.search_query);
    E.search_query = sstr_from(q->data, q->len);
    E.search_is_regex = 0;
}

/* Land the active cursor on match (my, mx): activate the existing
 * cursor there if one already holds that position (no duplicates),
 * otherwise add one and make it active. On a fresh cursor, drop the
 * selection if we were in VISUAL so subsequent edits are positional. */
static void mc_activate_match(Buffer *buf, Window *win, int my, int mx) {
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

/* C-n: add a cursor at the next occurrence of the word under cursor
 * (NORMAL) or the active visual selection (VISUAL, single-line only).
 * Skipped during replay — adding a cursor is a global action, not a
 * per-cursor edit, so we want exactly one new cursor per keypress. */
static void kb_mc_add_next_match(void) {
    if (in_replay) return;
    BUFWIN(buf, win)
    if (buf->num_rows == 0) return;
    /* The active cursor's stored position may trail win->cursor (motions
     * only move the window cursor) — snap it before it becomes a parked
     * cursor, or it would land where the cursor was long ago. */
    buf_cursor_sync_from_window(buf);

    SizedStr q = {0};
    int sy, sx, ex;
    if (!mc_query_at_cursor(buf, win, &q, &sy, &sx, &ex)) return;

    /* Search from one past the end of the current word / selection so
     * the occurrence at the cursor isn't matched again. */
    int my, mx;
    if (!mc_find_next(buf, q.data, q.len, sy, ex, &my, &mx)) {
        ed_set_status_message("multicursor: no more matches");
        sstr_free(&q);
        return;
    }

    mc_seed_search(&q);
    sstr_free(&q);
    mc_activate_match(buf, win, my, mx);
}

/* Mirror of kb_mc_add_next_match walking backward — leaves a cursor at
 * the current occurrence and advances the active cursor to the previous
 * match. */
static void kb_mc_add_prev_match(void) {
    if (in_replay) return;
    BUFWIN(buf, win)
    if (buf->num_rows == 0) return;
    buf_cursor_sync_from_window(buf);

    SizedStr q = {0};
    int sy, sx, ex;
    if (!mc_query_at_cursor(buf, win, &q, &sy, &sx, &ex)) return;
    (void)ex;

    /* Search backward from the start of the current word / selection
     * so we look for an earlier occurrence, not this one again. */
    int my, mx;
    if (!mc_find_prev(buf, q.data, q.len, sy, sx, &my, &mx)) {
        ed_set_status_message("multicursor: no more matches");
        sstr_free(&q);
        return;
    }

    mc_seed_search(&q);
    sstr_free(&q);
    mc_activate_match(buf, win, my, mx);
}

/* '*: put a cursor at every occurrence of the word under cursor
 * (NORMAL) or the single-line visual selection (VISUAL) in the whole
 * buffer. The existing cursor set is replaced — afterwards there is
 * exactly one cursor per match, the active one on the occurrence the
 * query came from. Matches are non-overlapping, scanned left to
 * right. */
static void kb_mc_match_all(void) {
    if (in_replay) return;
    BUFWIN(buf, win)
    if (buf->num_rows == 0) return;
    buf_cursor_sync_from_window(buf);

    SizedStr q = {0};
    int sy, sx, ex;
    if (!mc_query_at_cursor(buf, win, &q, &sy, &sx, &ex)) return;
    (void)ex;

    McPos *matches = NULL;
    for (int y = 0; y < buf->num_rows; y++) {
        Row *row = &buf->rows[y];
        int len = (int)row->chars.len;
        int x = 0;
        while (x + (int)q.len <= len) {
            if (memcmp(row->chars.data + x, q.data, q.len) == 0) {
                McPos p = { y, x };
                arrput(matches, p);
                x += (int)q.len;
            } else {
                x++;
            }
        }
    }

    int n = (int)arrlen(matches);
    if (n == 0) { /* can't happen — the query was read out of the buffer */
        ed_set_status_message("multicursor: no matches");
        arrfree(matches);
        sstr_free(&q);
        return;
    }

    mc_seed_search(&q);
    sstr_free(&q);

    /* Active match: the occurrence the query came from. If overlap
     * skipping ate that exact start position, fall back to the first
     * match at or after it, then to the first match overall. */
    int active = -1;
    for (int i = 0; i < n; i++) {
        if (matches[i].y == sy && matches[i].x == sx) { active = i; break; }
    }
    if (active < 0) {
        for (int i = 0; i < n; i++) {
            if (matches[i].y > sy ||
                (matches[i].y == sy && matches[i].x >= sx)) {
                active = i;
                break;
            }
        }
    }
    if (active < 0) active = 0;

    /* Rebuild the cursor set: one cursor per match, nothing else. */
    buf_cursor_clear_extras(buf);
    buf->cursor->y = matches[active].y;
    buf->cursor->x = matches[active].x;
    int oom = 0;
    for (int i = 0; i < n; i++) {
        if (i == active) continue;
        if (!buf_cursor_add(buf, matches[i].y, matches[i].x)) {
            oom = 1;
            break;
        }
    }
    win->cursor.y = matches[active].y;
    win->cursor.x = matches[active].x;
    arrfree(matches);

    if (E.mode == MODE_VISUAL) {
        win->sel.type = SEL_NONE;
        ed_set_mode(MODE_NORMAL);
    }
    if (oom)
        ed_set_status_message("multicursor: out of memory");
    else
        ed_set_status_message("multicursor: %d cursors (all matches)",
                              buf_cursor_count(buf));
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
    buf_cursor_sync_from_window(buf);
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

/* Move the active cursor to the next/previous cursor in cyclic (y,x)
 * order without dropping any — the way to hop between parked cursors
 * when sync is off. dir is +1 (next) or -1 (previous). */
static void mc_jump(int dir) {
    if (in_replay) return;
    BUFWIN(buf, win)
    int n = buf_cursor_count(buf);
    if (n <= 1) {
        ed_set_status_message("multicursor: only one cursor");
        return;
    }
    buf_cursor_sync_from_window(buf);
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

    int target = (idx + dir + n) % n;
    Cursor *next = sorted[target];
    free(sorted);

    buf->cursor = next;
    win->cursor.x = next->x;
    win->cursor.y = next->y;
    ed_set_status_message("multicursor: cursor %d/%d", target + 1, n);
}

static void kb_mc_jump_next(void) { mc_jump(+1); }
static void kb_mc_jump_prev(void) { mc_jump(-1); }

/* --- Commands --- */

/* Plant a stationary cursor at the current position and advance the
 * active cursor by one row in `dy`. Net effect: repeated C-Down "drops
 * a trail" of cursors as you descend (and C-Up as you ascend), which
 * mirrors how C-n leaves a cursor behind at each previous match. */
static void mc_add_and_advance(int dy) {
    if (in_replay) return;
    BUFWIN(buf, win)
    if (!buf->cursor) { ed_set_status_message("multicursor: no cursor"); return; }
    buf_cursor_sync_from_window(buf);
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

/* Plant a cursor at the current position and make the new one the
 * active cursor — the previous active stays parked here as a marker
 * to come back to with :mc_jump_next/_prev. */
static void mc_add_here(void) {
    if (in_replay) return;
    BUFWIN(buf, win)
    if (!buf->cursor) { ed_set_status_message("multicursor: no cursor"); return; }
    buf_cursor_sync_from_window(buf);
    /* Don't stack a third cursor on a spot that already has a parked one. */
    for (ptrdiff_t i = 0; i < arrlen(buf->all_cursors); i++) {
        Cursor *c = buf->all_cursors[i];
        if (c != buf->cursor && c->y == win->cursor.y && c->x == win->cursor.x) {
            ed_set_status_message("multicursor: cursor already here");
            return;
        }
    }
    Cursor *nc = buf_cursor_add(buf, win->cursor.y, win->cursor.x);
    if (!nc) {
        ed_set_status_message("multicursor: out of memory");
        return;
    }
    buf->cursor = nc;
    ed_set_status_message("multicursor: %d cursors", buf_cursor_count(buf));
}

/* Toggle a parked cursor at the current position: if one besides the
 * active cursor already sits here, remove it; otherwise plant one
 * (mc_add_here semantics — the new cursor becomes active, the previous
 * active stays parked here as the marker). */
static void mc_toggle_here(void) {
    if (in_replay) return;
    BUFWIN(buf, win)
    if (!buf->cursor) { ed_set_status_message("multicursor: no cursor"); return; }
    buf_cursor_sync_from_window(buf);
    for (ptrdiff_t i = 0; i < arrlen(buf->all_cursors); i++) {
        Cursor *c = buf->all_cursors[i];
        if (c != buf->cursor && c->y == win->cursor.y && c->x == win->cursor.x) {
            buf_cursor_remove(buf, c);
            ed_set_status_message("multicursor: %d cursors (removed here)",
                                  buf_cursor_count(buf));
            return;
        }
    }
    Cursor *nc = buf_cursor_add(buf, win->cursor.y, win->cursor.x);
    if (!nc) {
        ed_set_status_message("multicursor: out of memory");
        return;
    }
    buf->cursor = nc;
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

static void cmd_mc_add_here(const char *args) {
    (void)args;
    mc_add_here();
}

static void cmd_mc_toggle_here(const char *args) {
    (void)args;
    mc_toggle_here();
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

static void cmd_mc_match_all(const char *args) {
    (void)args;
    kb_mc_match_all();
}

static void cmd_mc_skip(const char *args) {
    (void)args;
    kb_mc_skip();
}

static void cmd_mc_jump_next(const char *args) {
    (void)args;
    mc_jump(+1);
}

static void cmd_mc_jump_prev(const char *args) {
    (void)args;
    mc_jump(-1);
}

static void cmd_mc_sync(const char *args) {
    /* Global switch, not a per-cursor edit — never run on a replay
     * (an even cursor count would toggle it back to where it was). */
    if (in_replay) return;
    if (!args || !*args || strcmp(args, "toggle") == 0) {
        mc_sync = !mc_sync;
    } else if (strcmp(args, "on") == 0) {
        mc_sync = 1;
    } else if (strcmp(args, "off") == 0) {
        mc_sync = 0;
    } else {
        ed_set_status_message("usage: mc_sync [on|off|toggle]");
        return;
    }
    ed_set_status_message("multicursor: sync %s%s", mc_sync ? "on" : "off",
                          mc_sync ? " (edits replay at every cursor)" : "");
}

static void cmd_mc_debug(const char *args) {
    if (in_replay) return; /* global switch — same replay hazard as mc_sync */
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

static void kb_mc_add_below(void)   { cmd_mc_add_below(NULL); }
static void kb_mc_add_above(void)   { cmd_mc_add_above(NULL); }
static void kb_mc_clear(void)       { cmd_mc_clear(NULL); }
static void kb_mc_sync_toggle(void) { cmd_mc_sync(NULL); }

/* --- lifecycle --- */

static int multicursor_init(void) {
    cmd("mc_add_below",  cmd_mc_add_below,  "multicursor: add cursor below");
    cmd("mc_add_above",  cmd_mc_add_above,  "multicursor: add cursor above");
    cmd("mc_add_here",   cmd_mc_add_here,   "multicursor: add cursor at current position");
    cmd("mc_toggle_here", cmd_mc_toggle_here, "multicursor: toggle cursor at current position");
    cmd("mc_clear",      cmd_mc_clear,      "multicursor: keep only active cursor");
    cmd("mc_count",      cmd_mc_count,      "multicursor: show cursor count");
    cmd("mc_next_match", cmd_mc_next_match, "multicursor: add cursor at next match");
    cmd("mc_prev_match", cmd_mc_prev_match, "multicursor: add cursor at previous match");
    cmd("mc_match_all",  cmd_mc_match_all,  "multicursor: cursor at every match");
    cmd("mc_skip",       cmd_mc_skip,       "multicursor: skip current cursor");
    cmd("mc_jump_next",  cmd_mc_jump_next,  "multicursor: jump to next cursor");
    cmd("mc_jump_prev",  cmd_mc_jump_prev,  "multicursor: jump to previous cursor");
    cmd("mc_sync",       cmd_mc_sync,       "multicursor: sync edits at all cursors [on|off|toggle]");
    cmd("mc_debug",      cmd_mc_debug,      "multicursor: toggle dispatch logging");

    /* ' cluster — same letters as <space>m. Bare ' must stay unbound:
     * keybind_process fires an exact match immediately, so binding it
     * would make every two-key 'x sequence unreachable. */
    mapn("''", mc_toggle_here,       "multicursor: toggle cursor here");
    mapn("'a", mc_add_here,          "multicursor: add cursor here");
    mapn("'d", kb_mc_add_below,      "multicursor: add cursor below");
    mapn("'u", kb_mc_add_above,      "multicursor: add cursor above");
    mapn("'c", kb_mc_clear,          "multicursor: clear extras");
    mapn("'j", kb_mc_jump_next,      "multicursor: jump to next cursor");
    mapn("'k", kb_mc_jump_prev,      "multicursor: jump to previous cursor");
    mapn("'s", kb_mc_sync_toggle,    "multicursor: toggle synced edits");
    mapn("'n", kb_mc_add_next_match, "multicursor: cursor at next match");
    mapn("'p", kb_mc_add_prev_match, "multicursor: cursor at previous match");
    mapn("'*", kb_mc_match_all,      "multicursor: cursor at every match");

    mapn("<C-Down>", kb_mc_add_below, "multicursor: add cursor below");
    mapn("<C-Up>",   kb_mc_add_above, "multicursor: add cursor above");
    mapi("<C-Down>", kb_mc_add_below, "multicursor: add cursor below");
    mapi("<C-Up>",   kb_mc_add_above, "multicursor: add cursor above");

    mapn("<C-n>", kb_mc_add_next_match, "multicursor: cursor at next match");
    mapn("<M-n>", kb_mc_match_all,      "multicursor: cursor at every match");

    /* In VISUAL these match the selected text and drop back to NORMAL. */
    mapv("<C-n>", kb_mc_add_next_match, "multicursor: cursor at next match of selection");
    mapv("<M-n>", kb_mc_match_all,      "multicursor: cursor at every match of selection");
    mapv("'n",    kb_mc_add_next_match, "multicursor: cursor at next match of selection");
    mapv("'p",    kb_mc_add_prev_match, "multicursor: cursor at previous match of selection");
    mapv("'*",    kb_mc_match_all,      "multicursor: cursor at every match of selection");

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
