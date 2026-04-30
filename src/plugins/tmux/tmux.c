/* tmux plugin: runner pane integration. Implementation lives in
 * tmux_impl.c next to this file. The plugin owns the activation
 * surface — commands and the send-line keybind.
 *
 * Note: command-mode history navigation in src/command_mode.c still
 * calls tmux_history_* directly. Those APIs remain in tmux.h and are
 * resolved via -Isrc/plugins/tmux. */

#include "../plugin.h"
#include "buf_helpers.h"
#include "hed.h"
#include "tmux.h"
#include <stdlib.h>
#include <string.h>

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

/* Send the paragraph under cursor to the runner pane. Exposed both as
 * a keybind callback and as the `:tmux_send_line` command. */
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
    tmux_send_command(cmd_str);

    free(cmd_str);
    sstr_free(&para);
}

static void cmd_tmux_send_line(const char *args) {
    (void)args;
    kb_tmux_send_line();
}

static int tmux_plugin_init(void) {
    cmd("tmux_toggle",    cmd_tmux_toggle,    "tmux toggle runner pane");
    cmd("tmux_send",      cmd_tmux_send,      "tmux send command");
    cmd("tmux_kill",      cmd_tmux_kill,      "tmux kill runner pane");
    cmd("tmux_send_line", cmd_tmux_send_line, "tmux send paragraph under cursor");
    mapn(" ts", kb_tmux_send_line, "send paragraph to tmux runner");
    return 0;
}

const Plugin plugin_tmux = {
    .name   = "tmux",
    .desc   = "tmux runner pane integration",
    .init   = tmux_plugin_init,
    .deinit = NULL,
};
