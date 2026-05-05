/* whichkey: when a multi-key sequence is partway typed, list the
 * candidate completions in the message bar. Driven entirely by the
 * HOOK_KEYBIND_FEED / HOOK_KEYBIND_INVOKE hooks; no polling, no extra
 * UI plumbing. The listing clears the moment a binding fires or the
 * sequence resolves. */

#include "hed.h"
#include "hooks.h"
#include "keybinds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Display caps. The status_msg buffer is 4 KB; leave headroom. */
#define MAX_LINES   18
#define MIN_COL_W   28   /* below this we drop to fewer columns */
#define COL_SEP     " | "  /* delimiter rendered between cells */
#define COL_SEP_W   3      /* width of COL_SEP */
#define MAX_COLS    4

/* Track whether we currently own the message bar so we don't clobber
 * unrelated status messages (e.g. ":w" feedback) on the next clear. */
static int wk_active = 0;

/* Runtime enable flag. When toggled off, hooks are unregistered so
 * the dispatcher pays nothing for whichkey at all. */
static int wk_enabled = 1;

static void on_feed(const HookKeybindFeedEvent *e);
static void on_invoke(const HookKeybindInvokeEvent *e);

static void wk_subscribe(void) {
    hook_register_keybind_feed(HOOK_KEYBIND_FEED, on_feed);
    hook_register_keybind_invoke(HOOK_KEYBIND_INVOKE, on_invoke);
}

static void wk_unsubscribe(void) {
    hook_unregister(HOOK_KEYBIND_FEED, (void *)on_feed);
    hook_unregister(HOOK_KEYBIND_INVOKE, (void *)on_invoke);
}

/* qsort isn't reentrant in standard C; stash the prefix length the
 * comparator needs in a file-scope variable. on_feed runs single-
 * threaded, so this is safe. */
static int wk_sort_prefix_len = 0;
static int wk_cmp_by_tail(const void *a, const void *b) {
    const KeybindMatchView *ma = *(const KeybindMatchView *const *)a;
    const KeybindMatchView *mb = *(const KeybindMatchView *const *)b;
    return strcmp(ma->sequence + wk_sort_prefix_len,
                  mb->sequence + wk_sort_prefix_len);
}

static void wk_clear(void) {
    if (wk_active) {
        ed_set_status_message("");
        wk_active = 0;
    }
}

static void on_feed(const HookKeybindFeedEvent *e) {
    if (!e) return;

    /* Don't render anything for digit-only counts or when there's
     * nothing partial waiting. The exact-match case clears so the
     * popup vanishes the instant a binding is fully typed. */
    if (!e->partial) {
        wk_clear();
        return;
    }

    char buf[3800];
    size_t off = 0;
    int  shown = 0;

    /* Header: current sequence (and count if any). */
    if (e->has_count) {
        off += (size_t)snprintf(buf + off, sizeof(buf) - off,
                                "%d %s\n", e->count,
                                e->active_sequence ? e->active_sequence : "");
    } else {
        off += (size_t)snprintf(buf + off, sizeof(buf) - off,
                                "%s\n", e->active_sequence ? e->active_sequence : "");
    }

    int prefix_len = e->active_len;
    int truncated  = 0;

    /* Collect the visible matches (drop the exact match — we're
     * showing what *more* the user could type) and sort by tail.
     * strcmp orders ASCII naturally, so space (0x20) lands above any
     * printable char — a leader-prefixed binding always floats to
     * the top. */
    const KeybindMatchView **sorted = NULL;
    int n = 0;
    if (e->match_count > 0) {
        sorted = malloc(sizeof(*sorted) * (size_t)e->match_count);
    }
    if (sorted) {
        for (int i = 0; i < e->match_count; i++) {
            const KeybindMatchView *m = &e->matches[i];
            if (!m->sequence) continue;
            if ((int)strlen(m->sequence) == prefix_len) continue;
            sorted[n++] = m;
        }
        wk_sort_prefix_len = prefix_len;
        qsort(sorted, (size_t)n, sizeof(*sorted), wk_cmp_by_tail);
    }

    /* Decide column count from terminal width: pick the largest N
     * (up to MAX_COLS) where N cells of MIN_COL_W plus separators
     * still fit. */
    int term_cols = E.screen_cols > 0 ? E.screen_cols : 80;
    int ncols = 1;
    for (int c = MAX_COLS; c >= 1; c--) {
        if (term_cols >= MIN_COL_W * c + COL_SEP_W * (c - 1)) {
            ncols = c;
            break;
        }
    }
    int col_w = (term_cols - COL_SEP_W * (ncols - 1)) / ncols;
    if (col_w < MIN_COL_W) col_w = MIN_COL_W;

    /* Pick the tail column width from the data, capped so descriptions
     * still get room. */
    int max_tail = 1;
    for (int i = 0; i < n; i++) {
        int t = (int)strlen(sorted[i]->sequence + prefix_len);
        if (t > max_tail) max_tail = t;
    }
    int tail_w = max_tail;
    if (tail_w > col_w / 3) tail_w = col_w / 3;
    int desc_w = col_w - tail_w - 2; /* 1 leading space, 1 between */
    if (desc_w < 4) desc_w = 4;

    int max_cells = MAX_LINES * ncols;
    int cells = n < max_cells ? n : max_cells;
    if (cells < n) truncated = 1;

    for (int i = 0; i < cells; i++) {
        const KeybindMatchView *m = sorted[i];
        const char *tail = m->sequence + prefix_len;
        const char *desc = m->desc ? m->desc : "";

        if (off + (size_t)col_w + 4 > sizeof(buf)) {
            truncated = 1;
            break;
        }

        /* Truncate tail / desc to their cell widths. */
        char tbuf[64], dbuf[256];
        snprintf(tbuf, sizeof(tbuf), "%.*s", tail_w, tail);
        snprintf(dbuf, sizeof(dbuf), "%.*s", desc_w, desc);

        off += (size_t)snprintf(buf + off, sizeof(buf) - off,
                                " %-*s %-*s", tail_w, tbuf, desc_w, dbuf);
        shown++;

        int col_idx = i % ncols;
        int last_col = (col_idx == ncols - 1) || (i == cells - 1);
        if (last_col) {
            if (off < sizeof(buf)) buf[off++] = '\n';
        } else {
            off += (size_t)snprintf(buf + off, sizeof(buf) - off, "%s", COL_SEP);
        }
    }
    (void)shown;
    free(sorted);

    if (truncated && off + 8 < sizeof(buf)) {
        off += (size_t)snprintf(buf + off, sizeof(buf) - off, " ...\n");
    }

    /* Drop trailing newline so the message bar doesn't reserve a blank row. */
    if (off > 0 && buf[off - 1] == '\n') {
        buf[off - 1] = '\0';
    }

    ed_set_status_message("%s", buf);
    wk_active = 1;
}

static void on_invoke(const HookKeybindInvokeEvent *e) {
    (void)e;
    wk_clear();
}

/* :whichkey [on|off|toggle] — query or change the runtime enable
 * state. Toggling subscribes/unsubscribes the hooks. */
static void cmd_whichkey(const char *args) {
    while (args && (*args == ' ' || *args == '\t')) args++;
    int want;
    if (!args || !*args || strcmp(args, "toggle") == 0) {
        want = !wk_enabled;
    } else if (strcmp(args, "on") == 0) {
        want = 1;
    } else if (strcmp(args, "off") == 0) {
        want = 0;
    } else {
        ed_set_status_message("whichkey: unknown arg '%s' (use on|off|toggle)", args);
        return;
    }

    if (want != wk_enabled) {
        if (want) {
            wk_subscribe();
        } else {
            wk_unsubscribe();
            wk_clear();
        }
        wk_enabled = want;
    }
    ed_set_status_message("whichkey: %s", wk_enabled ? "on" : "off");
}

static int whichkey_init(void) {
    wk_subscribe();
    cmd("whichkey", cmd_whichkey, "toggle whichkey popup");
    cmapn(" th", "whichkey toggle");
    return 0;
}

const Plugin plugin_whichkey = {
    .name   = "whichkey",
    .desc   = "list candidate completions when a multi-key sequence is partway typed",
    .init   = whichkey_init,
    .deinit = NULL,
};
