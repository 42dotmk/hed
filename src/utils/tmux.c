#include "hed.h"
#include "tmux.h"
#include "cmd_util.h"
#include <stdarg.h>

static char tmux_pane_id[64] = {0};
static int tmux_pane_id_set = 0;

static int tmux_systemf(const char *fmt, ...) {
    char cmd[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cmd, sizeof(cmd), fmt, ap);
    va_end(ap);
    return term_cmd_system(cmd);
}

int tmux_is_available(void) {
    const char *tmux = getenv("TMUX");
    return (tmux && *tmux) ? 1 : 0;
}

/* Check whether our cached pane id still refers to an existing pane
 * (in any window of the current session). */
static int tmux_pane_exists(void) {
    if (!tmux_pane_id_set || !tmux_pane_id[0]) return 0;

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

    /* Create a new vertical split in the CURRENT window and capture its pane_id.
     * Use -d so tmux keeps focus in the editor pane. */
    char **lines = NULL;
    int count = 0;
    if (!term_cmd_run("tmux split-window -v -d -P -F '#{pane_id}'", &lines, &count)) {
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

/* Get the current window id into out/outsz. Returns 1 on success, 0 on failure. */
static int tmux_get_current_window_id(char *out, size_t outsz) {
    if (!out || outsz == 0) return 0;

    char **lines = NULL;
    int count = 0;
    if (!term_cmd_run("tmux display-message -p '#{window_id}'", &lines, &count)) {
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
static int tmux_get_pane_window_id(const char *pane_id, char *out, size_t outsz) {
    if (!pane_id || !*pane_id || !out || outsz == 0) return 0;

    char **lines = NULL;
    int count = 0;
    if (!term_cmd_run("tmux list-panes -a -F '#{pane_id} #{window_id}'", &lines, &count)) {
        return 0;
    }

    int found = 0;
    for (int i = 0; i < count; i++) {
        if (!lines[i]) continue;
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
        /* Currently visible in this window -> hide by breaking into its own window. */
        int status = tmux_systemf("tmux break-pane -dP -s %s", tmux_pane_id);
        if (status != 0) {
            ed_set_status_message("tmux: failed to hide pane %s (status %d)", tmux_pane_id, status);
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

    int status = tmux_systemf("tmux send-keys -t %s %s Enter", tmux_pane_id, esc);
    if (status != 0) {
        ed_set_status_message("tmux: send-keys failed (status %d)", status);
        return 0;
    }

    ed_set_status_message("tmux: sent to pane %s", tmux_pane_id);
    return 1;
}
