#ifndef TMUX_H
#define TMUX_H

/*
 * TMUX INTEGRATION
 * =================
 *
 * Small helper for integrating hed with tmux. It manages a dedicated
 * "runner" pane and provides helpers to open/toggle it and send
 * commands to it.
 *
 * High‑level behavior:
 * - Detects whether we are running inside tmux (via $TMUX)
 * - Creates a new pane (vertical split) for running commands
 * - Remembers the created pane's ID and can toggle (show/hide) it
 * - Sends arbitrary command lines to the pane via tmux send-keys
 *
 * These helpers are wired to editor commands and keybindings so that
 * you can do things like:
 *   :tmux_toggle           -> toggle the runner pane
 *   :tmux_send echo hi     -> send "echo hi" + Enter to the runner pane
 *   <space>ts              -> send current line to the runner pane
 */

/* Return non-zero if tmux integration is available (inside a tmux session). */
int tmux_is_available(void);

/* Ensure the dedicated runner pane exists; create it if necessary. */
int tmux_ensure_pane(void);

/* Toggle the runner pane: create if missing, show/hide otherwise. */
int tmux_toggle_pane(void);

/* Kill the runner pane (if it exists). */
int tmux_kill_pane(void);

/* Send a command line to the runner pane (appends Enter). */
int tmux_send_command(const char *cmd);

/* tmux command history helpers (for :tmux_send browsing) */
void tmux_history_reset_browse(void);
int tmux_history_browse_up(const char *current_args, int current_len, char *out,
                           int out_cap);
int tmux_history_browse_down(char *out, int out_cap, int *restored);

#endif /* TMUX_H */
