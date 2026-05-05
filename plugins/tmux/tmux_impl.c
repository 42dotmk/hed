#include "tmux.h"
#include "hed.h"
#include <stdarg.h>
#include <sys/wait.h>

/* ---- Named-pane registry ------------------------------------------------
 *
 * Each registered pane gets a slot tracking its name, spawn command, and
 * the tmux pane id once created. History is currently scoped to the
 * runner pane only (it backs :tmux_send completion in command_mode.c) —
 * if a future caller needs per-pane history this can be lifted into the
 * slot.
 */

#define TMUX_PANES_MAX        8
#define TMUX_PANE_NAME_MAX    32
#define TMUX_PANE_SPAWN_MAX   256
#define TMUX_PANE_ID_MAX      64

typedef struct {
    char         name[TMUX_PANE_NAME_MAX];
    char         spawn_cmd[TMUX_PANE_SPAWN_MAX];
    char         pane_id[TMUX_PANE_ID_MAX];
    int          pane_id_set;
    int          in_use;
    TmuxSplitDir dir;
} TmuxPaneSlot;

static TmuxPaneSlot tmux_panes[TMUX_PANES_MAX];

/* The most recently focused/opened pane name. Falls back to "runner",
 * which the tmux plugin always registers. */
static char tmux_last_focused[TMUX_PANE_NAME_MAX] = "runner";

static void tmux_set_last_focused(const char *name) {
    if (!name || !*name)
        return;
    snprintf(tmux_last_focused, sizeof(tmux_last_focused), "%s", name);
}

const char *tmux_last_focused_pane(void) { return tmux_last_focused; }

static TmuxPaneSlot *tmux_pane_find(const char *name) {
    if (!name || !*name)
        return NULL;
    for (int i = 0; i < TMUX_PANES_MAX; i++) {
        if (tmux_panes[i].in_use && strcmp(tmux_panes[i].name, name) == 0)
            return &tmux_panes[i];
    }
    return NULL;
}

static TmuxPaneSlot *tmux_pane_find_or_warn(const char *name) {
    TmuxPaneSlot *slot = tmux_pane_find(name);
    if (!slot)
        ed_set_status_message("tmux: pane '%s' not registered",
                              name ? name : "(null)");
    return slot;
}

int tmux_pane_register(const char *name, const char *spawn_cmd,
                       TmuxSplitDir dir) {
    if (!name || !*name)
        return 0;
    if (strlen(name) >= TMUX_PANE_NAME_MAX) {
        ed_set_status_message("tmux: pane name '%s' too long", name);
        return 0;
    }

    TmuxPaneSlot *slot = tmux_pane_find(name);
    if (!slot) {
        for (int i = 0; i < TMUX_PANES_MAX; i++) {
            if (!tmux_panes[i].in_use) {
                slot = &tmux_panes[i];
                break;
            }
        }
        if (!slot) {
            ed_set_status_message("tmux: pane registry full");
            return 0;
        }
        slot->in_use = 1;
        snprintf(slot->name, sizeof(slot->name), "%s", name);
        slot->pane_id[0] = '\0';
        slot->pane_id_set = 0;
    }
    snprintf(slot->spawn_cmd, sizeof(slot->spawn_cmd), "%s",
             spawn_cmd ? spawn_cmd : "");
    slot->dir = dir;
    return 1;
}

int tmux_pane_attach(const char *name, const char *pane_id) {
    if (!name || !*name || !pane_id || !*pane_id)
        return 0;
    if (strlen(pane_id) >= TMUX_PANE_ID_MAX) {
        ed_set_status_message("tmux: pane id '%s' too long", pane_id);
        return 0;
    }
    TmuxPaneSlot *slot = tmux_pane_find(name);
    if (!slot) {
        if (!tmux_pane_register(name, NULL, TMUX_SPLIT_BELOW))
            return 0;
        slot = tmux_pane_find(name);
        if (!slot)
            return 0;
    }
    snprintf(slot->pane_id, sizeof(slot->pane_id), "%s", pane_id);
    slot->pane_id_set = 1;
    ed_set_status_message("tmux: attached %s -> %s", slot->name, slot->pane_id);
    return 1;
}

int tmux_pane_list(const char **out, int max) {
    if (!out || max <= 0)
        return 0;
    int n = 0;
    for (int i = 0; i < TMUX_PANES_MAX && n < max; i++) {
        if (tmux_panes[i].in_use)
            out[n++] = tmux_panes[i].name;
    }
    return n;
}

static char tmux_split_flag(TmuxSplitDir dir) {
    return dir == TMUX_SPLIT_RIGHT ? 'h' : 'v';
}

/* ---- Runner-pane history (used by :tmux_send completion) --------------- */

#define TMUX_HISTORY_MAX 64
static char *tmux_history[TMUX_HISTORY_MAX];
static int tmux_history_len = 0;
static int tmux_hist_idx = -1;
static char tmux_hist_saved[512];
static int tmux_hist_saved_len = 0;
static char tmux_hist_prefix[512];
static int tmux_hist_prefix_len = 0;

__attribute__((unused)) static void tmux_history_clear(void) {
    for (int i = 0; i < tmux_history_len; i++)
        free(tmux_history[i]);
    tmux_history_len = 0;
}

static void tmux_history_reset_browse(void) {
    tmux_hist_idx = -1;
    tmux_hist_saved_len = 0;
    tmux_hist_saved[0] = '\0';
    tmux_hist_prefix_len = 0;
    tmux_hist_prefix[0] = '\0';
}

static int tmux_hist_prefix_match(const char *entry) {
    if (!entry)
        return 0;
    for (int i = 0; i < tmux_hist_prefix_len; i++) {
        if (!entry[i] || entry[i] != tmux_hist_prefix[i])
            return 0;
    }
    return 1;
}

static void tmux_history_add(const char *cmd) {
    if (!cmd || !*cmd)
        return;
    /* Dedup existing entry */
    for (int i = 0; i < tmux_history_len; i++) {
        if (strcmp(tmux_history[i], cmd) == 0) {
            free(tmux_history[i]);
            memmove(&tmux_history[i], &tmux_history[i + 1],
                    (size_t)(tmux_history_len - i - 1) * sizeof(char *));
            tmux_history_len--;
            break;
        }
    }
    if (tmux_history_len == TMUX_HISTORY_MAX) {
        free(tmux_history[TMUX_HISTORY_MAX - 1]);
        tmux_history_len--;
    }
    memmove(&tmux_history[1], &tmux_history[0],
            (size_t)tmux_history_len * sizeof(char *));
    tmux_history[0] = strdup(cmd);
    if (tmux_history[0])
        tmux_history_len++;
    tmux_history_reset_browse();
}

int tmux_history_browse_up(const char *current_args, int current_len, char *out,
                           int out_cap) {
    if (!out || out_cap <= 0 || tmux_history_len == 0)
        return 0;
    if (!current_args)
        current_args = "";
    if (current_len < 0)
        current_len = 0;

    if (tmux_hist_idx == -1) {
        tmux_hist_saved_len = current_len;
        if (tmux_hist_saved_len > (int)sizeof(tmux_hist_saved) - 1)
            tmux_hist_saved_len = (int)sizeof(tmux_hist_saved) - 1;
        memcpy(tmux_hist_saved, current_args, (size_t)tmux_hist_saved_len);
        tmux_hist_saved[tmux_hist_saved_len] = '\0';

        tmux_hist_prefix_len = current_len;
        if (tmux_hist_prefix_len > (int)sizeof(tmux_hist_prefix) - 1)
            tmux_hist_prefix_len = (int)sizeof(tmux_hist_prefix) - 1;
        memcpy(tmux_hist_prefix, current_args, (size_t)tmux_hist_prefix_len);
        tmux_hist_prefix[tmux_hist_prefix_len] = '\0';
    }

    int start = (tmux_hist_idx == -1) ? 0 : tmux_hist_idx + 1;
    for (int i = start; i < tmux_history_len; i++) {
        if (tmux_hist_prefix_match(tmux_history[i])) {
            tmux_hist_idx = i;
            snprintf(out, (size_t)out_cap, "%s", tmux_history[i]);
            return 1;
        }
    }
    return 0;
}

int tmux_history_browse_down(char *out, int out_cap, int *restored) {
    if (restored)
        *restored = 0;
    if (!out || out_cap <= 0 || tmux_history_len == 0)
        return 0;
    if (tmux_hist_idx == -1)
        return 0;

    for (int i = tmux_hist_idx - 1; i >= 0; i--) {
        if (tmux_hist_prefix_match(tmux_history[i])) {
            tmux_hist_idx = i;
            snprintf(out, (size_t)out_cap, "%s", tmux_history[i]);
            return 1;
        }
    }

    tmux_hist_idx = -1;
    if (restored)
        *restored = 1;
    int n = tmux_hist_saved_len;
    if (n > out_cap - 1)
        n = out_cap - 1;
    memcpy(out, tmux_hist_saved, (size_t)n);
    out[n] = '\0';
    return 1;
}

/* ---- tmux helpers ------------------------------------------------------- */

static int tmux_systemf(const char *fmt, ...) {
    char cmd[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cmd, sizeof(cmd), fmt, ap);
    va_end(ap);
    int status = term_cmd_system(cmd);
    if (status == -1)
        return -1;
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return status;
}

int tmux_is_available(void) {
    const char *tmux = getenv("TMUX");
    return (tmux && *tmux) ? 1 : 0;
}

/* Check whether a slot's cached pane id still refers to a live tmux pane. */
static int tmux_pane_alive(TmuxPaneSlot *slot) {
    if (!slot || !slot->pane_id_set || !slot->pane_id[0])
        return 0;

    char **lines = NULL;
    int count = 0;
    if (!term_cmd_run("tmux list-panes -a -F '#{pane_id}'", &lines, &count)) {
        return 0;
    }

    int found = 0;
    for (int i = 0; i < count; i++) {
        if (lines[i] && strcmp(lines[i], slot->pane_id) == 0) {
            found = 1;
            break;
        }
    }
    term_cmd_free(lines, count);
    if (!found) {
        slot->pane_id_set = 0;
        slot->pane_id[0] = '\0';
    }
    return found;
}

static int tmux_pane_create(TmuxPaneSlot *slot) {
    char split_cmd[512];
    char flag = tmux_split_flag(slot->dir);
    if (slot->spawn_cmd[0]) {
        char esc[TMUX_PANE_SPAWN_MAX + 16];
        shell_escape_single(slot->spawn_cmd, esc, sizeof(esc));
        snprintf(split_cmd, sizeof(split_cmd),
                 "tmux split-window -%c -d -P -F '#{pane_id}' %s", flag, esc);
    } else {
        snprintf(split_cmd, sizeof(split_cmd),
                 "tmux split-window -%c -d -P -F '#{pane_id}'", flag);
    }

    char **lines = NULL;
    int count = 0;
    if (!term_cmd_run(split_cmd, &lines, &count)) {
        ed_set_status_message("tmux: failed to create %s pane", slot->name);
        return 0;
    }
    if (count <= 0 || !lines[0] || !lines[0][0]) {
        term_cmd_free(lines, count);
        ed_set_status_message("tmux: unexpected split-window output");
        return 0;
    }

    snprintf(slot->pane_id, sizeof(slot->pane_id), "%s", lines[0]);
    slot->pane_id_set = 1;
    term_cmd_free(lines, count);

    tmux_set_last_focused(slot->name);
    ed_set_status_message("tmux: opened %s pane %s", slot->name, slot->pane_id);
    return 1;
}

int tmux_pane_ensure(const char *name) {
    if (!tmux_is_available()) {
        ed_set_status_message("tmux: not inside tmux session");
        return 0;
    }
    TmuxPaneSlot *slot = tmux_pane_find_or_warn(name);
    if (!slot)
        return 0;
    if (tmux_pane_alive(slot))
        return 1;
    return tmux_pane_create(slot);
}

/* Get the current window id into out/outsz. Returns 1 on success, 0 on failure.
 */
static int tmux_get_current_window_id(char *out, size_t outsz) {
    if (!out || outsz == 0)
        return 0;

    char **lines = NULL;
    int count = 0;
    if (!term_cmd_run("tmux display-message -p '#{window_id}'", &lines,
                      &count)) {
        return 0;
    }
    if (count <= 0 || !lines[0] || !lines[0][0]) {
        term_cmd_free(lines, count);
        return 0;
    }

    snprintf(out, outsz, "%s", lines[0]);
    term_cmd_free(lines, count);
    return 1;
}

/* Look up the window id that currently owns pane_id. */
static int tmux_get_pane_window_id(const char *pane_id, char *out,
                                   size_t outsz) {
    if (!pane_id || !*pane_id || !out || outsz == 0)
        return 0;

    char **lines = NULL;
    int count = 0;
    if (!term_cmd_run("tmux list-panes -a -F '#{pane_id} #{window_id}'", &lines,
                      &count)) {
        return 0;
    }

    int found = 0;
    for (int i = 0; i < count; i++) {
        if (!lines[i])
            continue;
        char pid[64] = {0};
        char wid[64] = {0};
        if (sscanf(lines[i], "%63s %63s", pid, wid) == 2) {
            if (strcmp(pid, pane_id) == 0) {
                snprintf(out, outsz, "%s", wid);
                found = 1;
                break;
            }
        }
    }

    term_cmd_free(lines, count);
    return found;
}

int tmux_pane_toggle(const char *name) {
    if (!tmux_is_available()) {
        ed_set_status_message("tmux: not inside tmux session");
        return 0;
    }
    TmuxPaneSlot *slot = tmux_pane_find_or_warn(name);
    if (!slot)
        return 0;

    /* No cached/valid pane – create one attached to this window. */
    if (!tmux_pane_alive(slot)) {
        return tmux_pane_create(slot);
    }

    /* Pane exists somewhere: toggle between visible in this window and "hidden"
     * (parked in its own window). */
    char cur_wid[64] = {0};
    char pane_wid[64] = {0};
    if (!tmux_get_current_window_id(cur_wid, sizeof(cur_wid)) ||
        !tmux_get_pane_window_id(slot->pane_id, pane_wid, sizeof(pane_wid))) {
        ed_set_status_message("tmux: failed to query pane/window");
        return 0;
    }

    if (strcmp(cur_wid, pane_wid) == 0) {
        /* Currently visible in this window -> hide by breaking into its own
         * window. */
        int status = tmux_systemf("tmux break-pane -dP -s %s", slot->pane_id);
        if (status != 0) {
            /* Fallback: if breaking fails, try killing the pane so next toggle
             * recreates it. */
            int kill_status =
                tmux_systemf("tmux kill-pane -t %s", slot->pane_id);
            slot->pane_id_set = 0;
            slot->pane_id[0] = '\0';
            if (kill_status == 0) {
                ed_set_status_message(
                    "tmux: %s pane closed (break failed: %d)", slot->name,
                    status);
                return 1;
            }
            ed_set_status_message("tmux: failed to hide %s pane (status %d)",
                                  slot->name, status);
            return 0;
        }
        ed_set_status_message("tmux: hid %s pane", slot->name);
        return 1;
    } else {
        /* Pane is in another window -> show by joining it into this window. */
        int status = tmux_systemf("tmux join-pane -%c -d -s %s",
                                  tmux_split_flag(slot->dir), slot->pane_id);
        if (status != 0) {
            ed_set_status_message("tmux: failed to show %s pane (status %d)",
                                  slot->name, status);
            return 0;
        }
        tmux_set_last_focused(slot->name);
        ed_set_status_message("tmux: showed %s pane", slot->name);
        return 1;
    }
}

int tmux_pane_focus(const char *name) {
    if (!tmux_is_available()) {
        ed_set_status_message("tmux: not inside tmux session");
        return 0;
    }
    TmuxPaneSlot *slot = tmux_pane_find_or_warn(name);
    if (!slot)
        return 0;

    if (!tmux_pane_alive(slot)) {
        if (!tmux_pane_create(slot))
            return 0;
    } else {
        char cur_wid[64] = {0};
        char pane_wid[64] = {0};
        if (tmux_get_current_window_id(cur_wid, sizeof(cur_wid)) &&
            tmux_get_pane_window_id(slot->pane_id, pane_wid,
                                    sizeof(pane_wid)) &&
            strcmp(cur_wid, pane_wid) != 0) {
            int status =
                tmux_systemf("tmux join-pane -%c -d -s %s",
                             tmux_split_flag(slot->dir), slot->pane_id);
            if (status != 0) {
                ed_set_status_message(
                    "tmux: failed to show %s pane (status %d)", slot->name,
                    status);
                return 0;
            }
        }
    }

    int status = tmux_systemf("tmux select-pane -t %s", slot->pane_id);
    if (status != 0) {
        ed_set_status_message("tmux: failed to focus %s pane (status %d)",
                              slot->name, status);
        return 0;
    }
    tmux_set_last_focused(slot->name);
    ed_set_status_message("tmux: focused %s pane", slot->name);
    return 1;
}

int tmux_pane_kill(const char *name) {
    if (!tmux_is_available()) {
        ed_set_status_message("tmux: not inside tmux session");
        return 0;
    }
    TmuxPaneSlot *slot = tmux_pane_find_or_warn(name);
    if (!slot)
        return 0;
    if (!tmux_pane_alive(slot)) {
        ed_set_status_message("tmux: no %s pane", slot->name);
        return 0;
    }

    int status = tmux_systemf("tmux kill-pane -t %s", slot->pane_id);
    if (status != 0) {
        ed_set_status_message("tmux: failed to kill %s pane (status %d)",
                              slot->name, status);
        return 0;
    }
    slot->pane_id_set = 0;
    slot->pane_id[0] = '\0';
    ed_set_status_message("tmux: killed %s pane", slot->name);
    return 1;
}

int tmux_pane_send(const char *name, const char *cmd) {
    if (!cmd || !*cmd) {
        ed_set_status_message("tmux: empty command");
        return 0;
    }
    if (!tmux_pane_ensure(name)) {
        /* tmux_pane_ensure already set an error status message. */
        return 0;
    }
    TmuxPaneSlot *slot = tmux_pane_find(name);
    if (!slot)
        return 0;

    char esc[1024];
    shell_escape_single(cmd, esc, sizeof(esc));

    int status =
        tmux_systemf("tmux send-keys -t %s %s Enter", slot->pane_id, esc);
    if (status != 0) {
        ed_set_status_message("tmux: send-keys failed (status %d)", status);
        return 0;
    }

    /* History tracking is runner-pane only for now (it feeds :tmux_send
     * completion). Other panes get send without history. */
    if (strcmp(slot->name, "runner") == 0)
        tmux_history_add(cmd);

    ed_set_status_message("tmux: sent to %s pane %s", slot->name,
                          slot->pane_id);
    return 1;
}

int tmux_pane_send_focused(const char *cmd) {
    return tmux_pane_send(tmux_last_focused_pane(), cmd);
}

/* ---- Single-pane convenience API (operates on "runner") ---------------- */

int tmux_ensure_pane(void)              { return tmux_pane_ensure("runner"); }
int tmux_toggle_pane(void)              { return tmux_pane_toggle("runner"); }
int tmux_kill_pane(void)                { return tmux_pane_kill("runner"); }
int tmux_send_command(const char *cmd)  { return tmux_pane_send("runner", cmd); }
