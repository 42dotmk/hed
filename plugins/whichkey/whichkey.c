/* whichkey: when a multi-key sequence is partway typed, list the
 * candidate completions in the message bar. Driven entirely by the
 * HOOK_KEYBIND_FEED / HOOK_KEYBIND_INVOKE hooks plus a one-shot timer
 * registered with the select loop. The popup appears after a brief
 * idle delay so a quickly-typed chord (e.g. dd, gg) never flashes
 * the panel; if you keep typing within the delay window the timer
 * is rescheduled. */

#include "hed.h"
#include "hooks.h"
#include "keybinds.h"
#include "commands.h"
#include "select_loop.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Display caps. The status_msg buffer is 4 KB; leave headroom. */
#define MAX_LINES   18
#define MIN_COL_W   28   /* below this we drop to fewer columns */
#define COL_SEP     " | "  /* delimiter rendered between cells */
#define COL_SEP_W   3      /* width of COL_SEP */
#define MAX_COLS    4

#define WK_TIMER_NAME    "whichkey"
#define WK_DEFAULT_DELAY 300  /* ms */

/* Track whether we currently own the message bar so we don't clobber
 * unrelated status messages (e.g. ":w" feedback) on the next clear. */
static int wk_active  = 0;
static int wk_enabled = 1;
static int wk_delay_ms = WK_DEFAULT_DELAY;

/* Buffer holding the most recent rendered content. The timer callback
 * flushes whatever's here when it fires; on_feed overwrites it on
 * every keystroke so the popup always reflects the latest state. */
static char wk_pending[3800];
static int  wk_pending_ready = 0;

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
    ed_loop_timer_cancel(WK_TIMER_NAME);
    wk_pending_ready = 0;
    if (wk_active) {
        ed_set_status_message("");
        wk_active = 0;
    }
}

/* Format the partial-match table for `e` into `out`. Returns the
 * number of bytes written (NUL-terminated). */
static size_t wk_format(const HookKeybindFeedEvent *e, char *out, size_t outsz) {
    size_t off = 0;

    /* Header: current sequence (and count if any). */
    if (e->has_count) {
        off += (size_t)snprintf(out + off, outsz - off,
                                "%d %s\n", e->count,
                                e->active_sequence ? e->active_sequence : "");
    } else {
        off += (size_t)snprintf(out + off, outsz - off,
                                "%s\n", e->active_sequence ? e->active_sequence : "");
    }

    int prefix_len = e->active_len;
    int truncated  = 0;

    /* Collect visible matches (drop the exact match — we're showing
     * what *more* the user could type) and sort by tail. ASCII puts
     * space (0x20) before any printable char, so a leader-prefixed
     * binding always floats to the top. */
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

    /* Pick column count from terminal width: largest N (up to
     * MAX_COLS) where N MIN_COL_W cells plus separators fit. */
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
    int desc_w = col_w - tail_w - 2;
    if (desc_w < 4) desc_w = 4;

    int max_cells = MAX_LINES * ncols;
    int cells = n < max_cells ? n : max_cells;
    if (cells < n) truncated = 1;

    for (int i = 0; i < cells; i++) {
        const KeybindMatchView *m = sorted[i];
        const char *tail = m->sequence + prefix_len;
        const char *desc = m->desc;

        /* Fall back to the underlying command's description when the
         * keybinding itself doesn't carry one (typical for cmap*
         * bindings registered without a desc). */
        char cmdname[64];
        if ((!desc || !*desc) && m->is_command && m->cmdline) {
            const char *p = m->cmdline;
            while (*p == ' ' || *p == '\t' || *p == ':') p++;
            const char *end = p;
            while (*end && *end != ' ' && *end != '\t') end++;
            size_t len = (size_t)(end - p);
            if (len > 0 && len < sizeof(cmdname)) {
                memcpy(cmdname, p, len);
                cmdname[len] = '\0';
                const char *cdesc = command_find_desc(cmdname);
                if (cdesc && *cdesc) desc = cdesc;
            }
        }
        if (!desc) desc = m->cmdline ? m->cmdline : "";

        if (off + (size_t)col_w + 4 > outsz) {
            truncated = 1;
            break;
        }

        char tbuf[64], dbuf[256];
        snprintf(tbuf, sizeof(tbuf), "%.*s", tail_w, tail);
        snprintf(dbuf, sizeof(dbuf), "%.*s", desc_w, desc);

        off += (size_t)snprintf(out + off, outsz - off,
                                " %-*s %-*s", tail_w, tbuf, desc_w, dbuf);

        int col_idx = i % ncols;
        int last_col = (col_idx == ncols - 1) || (i == cells - 1);
        if (last_col) {
            if (off < outsz) out[off++] = '\n';
        } else {
            off += (size_t)snprintf(out + off, outsz - off, "%s", COL_SEP);
        }
    }
    free(sorted);

    if (truncated && off + 8 < outsz) {
        off += (size_t)snprintf(out + off, outsz - off, " ...\n");
    }

    /* Drop trailing newline so the message bar doesn't reserve a
     * blank row. */
    if (off > 0 && out[off - 1] == '\n') {
        out[off - 1] = '\0';
        off--;
    }
    return off;
}

/* Timer fires after the idle delay. Push whatever's currently in
 * wk_pending to the status bar. */
static void wk_timer_fire(void *ud) {
    (void)ud;
    if (!wk_pending_ready) return;
    ed_set_status_message("%s", wk_pending);
    wk_active = 1;
}

static void on_feed(const HookKeybindFeedEvent *e) {
    if (!e) return;

    if (!e->partial) {
        wk_clear();
        return;
    }

    /* Re-render into the pending buffer and (re)arm the timer. If
     * the user keeps typing within the delay window, the timer is
     * replaced — only the final, idle state ends up displayed. */
    wk_format(e, wk_pending, sizeof(wk_pending));
    wk_pending_ready = 1;

    /* If we're already showing the popup (the user paused, then
     * typed another key without resolving), refresh immediately so
     * it stays in sync with the new prefix. */
    if (wk_active) {
        ed_set_status_message("%s", wk_pending);
    }

    ed_loop_timer_after(WK_TIMER_NAME, wk_delay_ms, wk_timer_fire, NULL);
}

static void on_invoke(const HookKeybindInvokeEvent *e) {
    (void)e;
    wk_clear();
}

/* :whichkey [on|off|toggle|delay <ms>] */
static void cmd_whichkey(const char *args) {
    while (args && (*args == ' ' || *args == '\t')) args++;

    if (args && strncmp(args, "delay", 5) == 0 &&
        (args[5] == ' ' || args[5] == '\t' || args[5] == '\0')) {
        const char *p = args + 5;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) {
            ed_set_status_message("whichkey: delay = %d ms", wk_delay_ms);
            return;
        }
        char *end = NULL;
        long v = strtol(p, &end, 10);
        if (end == p || v < 0 || v > 60000) {
            ed_set_status_message("whichkey: bad delay '%s' (0..60000 ms)", p);
            return;
        }
        wk_delay_ms = (int)v;
        ed_set_status_message("whichkey: delay = %d ms", wk_delay_ms);
        return;
    }

    int want;
    if (!args || !*args || strcmp(args, "toggle") == 0) {
        want = !wk_enabled;
    } else if (strcmp(args, "on") == 0) {
        want = 1;
    } else if (strcmp(args, "off") == 0) {
        want = 0;
    } else {
        ed_set_status_message("whichkey: unknown arg '%s' (use on|off|toggle|delay <ms>)", args);
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
    ed_set_status_message("whichkey: %s (delay %d ms)",
                          wk_enabled ? "on" : "off", wk_delay_ms);
}

static int whichkey_init(void) {
    wk_subscribe();
    cmd("whichkey", cmd_whichkey, "toggle whichkey popup");
    cmapn(" th", "whichkey toggle", "toggle whichkey");
    return 0;
}

const Plugin plugin_whichkey = {
    .name   = "whichkey",
    .desc   = "list candidate completions when a multi-key sequence is partway typed (with idle delay)",
    .init   = whichkey_init,
    .deinit = NULL,
};
