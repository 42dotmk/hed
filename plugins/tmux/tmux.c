/* tmux plugin: runner pane integration. Implementation lives in
 * tmux_impl.c next to this file. The plugin owns the activation
 * surface — commands and the send-line keybind.
 *
 * Note: command-mode history navigation in src/command_mode.c still
 * calls tmux_history_* directly. Those APIs remain in tmux.h and are
 * resolved via -Isrc/plugins/tmux. */

#include "hed.h"
#include "tmux.h"
#include "utils/fzf.h"

static void cmd_tmux_toggle(const char *args) {
    (void)args;
    tmux_toggle_pane();
}

static void cmd_tmux_send(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("Usage: :tmux_send <command>");
        return;
    }
    tmux_send_command(args);
}

static void cmd_tmux_kill(const char *args) {
    (void)args;
    tmux_kill_pane();
}

static void cmd_tmux_focus(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("Usage: :tmux_focus <name>");
        return;
    }
    tmux_pane_focus(args);
}

static void cmd_tmux_attach(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("Usage: :tmux_attach <name>");
        return;
    }
    char **sel = NULL;
    int cnt = 0;
    const char *list_cmd =
        "tmux list-panes -a -F "
        "'#{pane_id} #{session_name}:#{window_name}.#{pane_index} "
        "#{pane_current_command} | #{pane_title}'";
    if (!fzf_run(list_cmd, 0, &sel, &cnt) || cnt <= 0 || !sel[0] || !sel[0][0]) {
        fzf_free(sel, cnt);
        return;
    }
    /* First whitespace-separated field of the picked line is the pane id. */
    char pane_id[64] = {0};
    if (sscanf(sel[0], "%63s", pane_id) != 1 || !pane_id[0]) {
        ed_set_status_message("tmux: could not parse pane id");
        fzf_free(sel, cnt);
        return;
    }
    tmux_pane_attach(args, pane_id);
    fzf_free(sel, cnt);
}

#define TMUX_PANE_LIST_MAX 16

static void cmd_tmux_panes(const char *args) {
    (void)args;
    const char *names[TMUX_PANE_LIST_MAX];
    int n = tmux_pane_list(names, TMUX_PANE_LIST_MAX);
    if (n <= 0) {
        ed_set_status_message("tmux: no registered panes");
        return;
    }

    char **sel = NULL;
    int cnt = 0;
    if (!fzf_pick_list(names, n, 0, &sel, &cnt) || cnt <= 0 || !sel[0] ||
        !sel[0][0]) {
        fzf_free(sel, cnt);
        return;
    }
    tmux_pane_focus(sel[0]);
    fzf_free(sel, cnt);
}

/* Send the paragraph under cursor to whichever pane was last focused.
 * Exposed both as a keybind callback and as the `:tmux_send_line`
 * command. Defaults to the runner pane until something else is focused. */
static void kb_tmux_send_line(void) {
    SizedStr para = sstr_new();
    if (!buf_get_paragraph_under_cursor(&para) || para.len == 0) {
        sstr_free(&para);
        ed_set_status_message("tmux: no paragraph to send");
        return;
    }

    char *cmd_str = malloc(para.len + 1);
    if (!cmd_str) {
        sstr_free(&para);
        ed_set_status_message("tmux: out of memory");
        return;
    }

    memcpy(cmd_str, para.data, para.len);
    cmd_str[para.len] = '\0';
    tmux_pane_send_focused(cmd_str);

    free(cmd_str);
    sstr_free(&para);
}

static void cmd_tmux_send_line(const char *args) {
    (void)args;
    kb_tmux_send_line();
}

static int tmux_plugin_init(void) {
    /* Default "runner" pane: empty spawn cmd = user's login shell, split
     * below the editor. Other plugins can register their own panes via
     * tmux_pane_register(). */
    tmux_pane_register("runner", NULL, TMUX_SPLIT_BELOW);

    cmd("tmux_toggle",    cmd_tmux_toggle,    "tmux toggle runner pane");
    cmd("tmux_send",      cmd_tmux_send,      "tmux send command");
    cmd("tmux_kill",      cmd_tmux_kill,      "tmux kill runner pane");
    cmd("tmux_send_line", cmd_tmux_send_line, "tmux send paragraph to last focused pane");
    cmd("tmux_focus",     cmd_tmux_focus,     "tmux focus pane by name");
    cmd("tmux_panes",     cmd_tmux_panes,     "tmux fzf-pick a registered pane");
    cmd("tmux_attach",    cmd_tmux_attach,    "tmux bind a live pane to a name");
    mapn(" ts", kb_tmux_send_line, "send paragraph to last focused tmux pane");
    return 0;
}

const Plugin plugin_tmux = {
    .name   = "tmux",
    .desc   = "tmux runner pane integration",
    .init   = tmux_plugin_init,
    .deinit = NULL,
};
