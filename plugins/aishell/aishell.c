/* aishell plugin: opens an AI shell (Claude Code, Copilot, etc.) in a
 * dedicated tmux pane.
 *
 * Piggybacks on the tmux plugin's named-pane registry — the pane is
 * registered as "aishell" with a configurable spawn command (default:
 * "claude"), so toggling brings up your AI assistant. The lifecycle
 * (create on first toggle, hide via break-pane, show via join-pane,
 * kill) is the same as the runner pane.
 *
 * Configure via aishell_set_spawn_cmd("your_command") in config_init(). */

#include "hed.h"
#include "aishell.h"
#include "tmux/tmux.h"

#include <string.h>
#include <stdio.h>

#define AISHELL_PANE_NAME "aishell"
#define AISHELL_SPAWN_CMD_MAX 128

static char spawn_cmd[AISHELL_SPAWN_CMD_MAX] = "claude";

void aishell_set_spawn_cmd(const char *cmd) {
    if (cmd && *cmd)
        snprintf(spawn_cmd, sizeof(spawn_cmd), "%s", cmd);
}

static void cmd_aishell_toggle(const char *args) {
    (void)args;
    tmux_pane_toggle(AISHELL_PANE_NAME);
}

static void cmd_aishell_kill(const char *args) {
    (void)args;
    tmux_pane_kill(AISHELL_PANE_NAME);
}

static void cmd_aishell_send(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("Usage: :aishell_send <text>");
        return;
    }
    tmux_pane_send(AISHELL_PANE_NAME, args);
}

static int aishell_plugin_init(void) {
    tmux_pane_register(AISHELL_PANE_NAME, spawn_cmd, TMUX_SPLIT_RIGHT);

    cmd("ai_toggle", cmd_aishell_toggle, "toggle AI shell pane");
    cmd("ai_kill",   cmd_aishell_kill,   "kill AI shell pane");
    cmd("ai_send",   cmd_aishell_send,   "send text to AI shell pane");
    return 0;
}

const Plugin plugin_aishell = {
    .name   = "aishell",
    .desc   = "AI shell (Claude Code, etc.) in a dedicated tmux pane",
    .init   = aishell_plugin_init,
    .deinit = NULL,
};
