/* claude plugin: opens Claude Code in a dedicated tmux pane.
 *
 * Piggybacks on the tmux plugin's named-pane registry — the pane is
 * registered as "claude" with `claude` as its spawn command, so toggling
 * brings up Claude Code instead of a shell. The lifecycle (create on
 * first toggle, hide via break-pane, show via join-pane, kill) is the
 * same as the runner pane. */

#include "hed.h"
#include "claude.h"
#include "tmux/tmux.h"

#define CLAUDE_PANE_NAME "claude"
#define CLAUDE_SPAWN_CMD "claude"

static void cmd_claude_toggle(const char *args) {
    (void)args;
    tmux_pane_toggle(CLAUDE_PANE_NAME);
}

static void cmd_claude_kill(const char *args) {
    (void)args;
    tmux_pane_kill(CLAUDE_PANE_NAME);
}

static void cmd_claude_send(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("Usage: :claude_send <text>");
        return;
    }
    tmux_pane_send(CLAUDE_PANE_NAME, args);
}

static int claude_plugin_init(void) {
    tmux_pane_register(CLAUDE_PANE_NAME, CLAUDE_SPAWN_CMD, TMUX_SPLIT_RIGHT);

    cmd("claude_toggle", cmd_claude_toggle, "toggle claude code pane");
    cmd("claude_kill",   cmd_claude_kill,   "kill claude code pane");
    cmd("claude_send",   cmd_claude_send,   "send text to claude pane");
    return 0;
}

const Plugin plugin_claude = {
    .name   = "claude",
    .desc   = "Claude Code in a dedicated tmux pane",
    .init   = claude_plugin_init,
    .deinit = NULL,
};
