#include "tmux.h"
#include "cmd_util.h"
#include "hed.h"
#include <stdarg.h>
#include <sys/wait.h>

static char tmux_pane_id[64] = {0};
static int tmux_pane_id_set = 0;

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

void tmux_history_reset_browse(void) {
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

static int tmux_systemf(const char *fmt, ...) {
    char cmd[256];
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

/* Check whether our cached pane id still refers to an existing pane
 * (in any window of the current session). */
static int tmux_pane_exists(void) {
    if (!tmux_pane_id_set || !tmux_pane_id[0])
        return 0;

    char **lines = NULL;
    int count = 0;
    if (!term_cmd_run("tmux list-panes -a -F '#{pane_id}'", &lines, &count)) {
        return 0;
    }

    int found = 0;
    for (int i = 0; i < count; i++) {
        if (lines[i] && strcmp(lines[i], tmux_pane_id) == 0) {
            found = 1;
            break;
        }
    }
    term_cmd_free(lines, count);
    if (!found) {
        tmux_pane_id_set = 0;
        tmux_pane_id[0] = '\0';
    }
    return found;
}

int tmux_ensure_pane(void) {
    if (!tmux_is_available()) {
        ed_set_status_message("tmux: not inside tmux session");
        return 0;
    }

    /* Reuse existing runner pane if it is still alive. */
    if (tmux_pane_exists()) {
        return 1;
    }

    /* Create a new vertical split in the CURRENT window and capture its
     * pane_id. Use -d so tmux keeps focus in the editor pane. */
    char **lines = NULL;
    int count = 0;
    if (!term_cmd_run("tmux split-window -v -d -P -F '#{pane_id}'", &lines,
                      &count)) {
        ed_set_status_message("tmux: failed to create pane");
        return 0;
    }
    if (count <= 0 || !lines[0] || !lines[0][0]) {
        term_cmd_free(lines, count);
        ed_set_status_message("tmux: unexpected split-window output");
        return 0;
    }

    snprintf(tmux_pane_id, sizeof(tmux_pane_id), "%s", lines[0]);
    tmux_pane_id_set = 1;
    term_cmd_free(lines, count);

    ed_set_status_message("tmux: opened runner pane %s", tmux_pane_id);
    return 1;
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

int tmux_toggle_pane(void) {
    if (!tmux_is_available()) {
        ed_set_status_message("tmux: not inside tmux session");
        return 0;
    }

    /* No cached/valid pane – create one attached to this window. */
    if (!tmux_pane_exists()) {
        return tmux_ensure_pane();
    }

    /* Pane exists somewhere: toggle between visible in this window and "hidden"
     * (parked in its own window). */
    char cur_wid[64] = {0};
    char pane_wid[64] = {0};
    if (!tmux_get_current_window_id(cur_wid, sizeof(cur_wid)) ||
        !tmux_get_pane_window_id(tmux_pane_id, pane_wid, sizeof(pane_wid))) {
        ed_set_status_message("tmux: failed to query pane/window");
        return 0;
    }

    if (strcmp(cur_wid, pane_wid) == 0) {
        /* Currently visible in this window -> hide by breaking into its own
         * window. */
        int status = tmux_systemf("tmux break-pane -dP -s %s", tmux_pane_id);
        if (status != 0) {
            /* Fallback: if breaking fails, try killing the pane so next toggle
             * recreates it. */
            int kill_status =
                tmux_systemf("tmux kill-pane -t %s", tmux_pane_id);
            tmux_pane_id_set = 0;
            tmux_pane_id[0] = '\0';
            if (kill_status == 0) {
                ed_set_status_message(
                    "tmux: runner pane closed (break failed: %d)", status);
                return 1;
            }
            ed_set_status_message("tmux: failed to hide pane %s (status %d)",
                                  tmux_pane_id, status);
            return 0;
        }
        ed_set_status_message("tmux: hid runner pane");
        return 1;
    } else {
        /* Pane is in another window -> show by joining it into this window. */
        int status = tmux_systemf("tmux join-pane -v -d -s %s", tmux_pane_id);
        if (status != 0) {
            ed_set_status_message("tmux: failed to show pane %s (status %d)",
                                  tmux_pane_id, status);
            return 0;
        }
        ed_set_status_message("tmux: showed runner pane");
        return 1;
    }
}

int tmux_kill_pane(void) {
    if (!tmux_is_available()) {
        ed_set_status_message("tmux: not inside tmux session");
        return 0;
    }
    if (!tmux_pane_exists()) {
        ed_set_status_message("tmux: no runner pane");
        return 0;
    }

    int status = tmux_systemf("tmux kill-pane -t %s", tmux_pane_id);
    if (status != 0) {
        ed_set_status_message("tmux: failed to kill pane %s (status %d)",
                              tmux_pane_id, status);
        return 0;
    }
    tmux_pane_id_set = 0;
    tmux_pane_id[0] = '\0';
    ed_set_status_message("tmux: killed runner pane");
    return 1;
}

int tmux_send_command(const char *cmd) {
    if (!cmd || !*cmd) {
        ed_set_status_message("tmux: empty command");
        return 0;
    }
    if (!tmux_ensure_pane()) {
        /* tmux_ensure_pane already set an error status message. */
        return 0;
    }

    char esc[1024];
    shell_escape_single(cmd, esc, sizeof(esc));

    int status =
        tmux_systemf("tmux send-keys -t %s %s Enter", tmux_pane_id, esc);
    if (status != 0) {
        ed_set_status_message("tmux: send-keys failed (status %d)", status);
        return 0;
    }

    tmux_history_add(cmd);
    ed_set_status_message("tmux: sent to pane %s", tmux_pane_id);
    return 1;
}
