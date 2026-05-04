#ifndef TMUX_H
#define TMUX_H

/*
 * TMUX INTEGRATION
 * =================
 *
 * Helpers for opening, toggling, and sending text to tmux panes from
 * inside hed. Two layers:
 *
 *   1. Named-pane registry — any plugin can register its own pane
 *      with a spawn command and drive it through tmux_pane_*.
 *   2. Single-pane convenience API — tmux_{ensure,toggle,kill}_pane
 *      and tmux_send_command operate on the built-in "runner" pane
 *      registered by this plugin's init().
 *
 * Detects a tmux session via $TMUX. Panes are vertical splits in the
 * current window; toggle hides them via break-pane (parking in their
 * own window) and shows them again via join-pane.
 *
 * Examples:
 *   :tmux_toggle           -> toggle the runner pane
 *   :tmux_send echo hi     -> send "echo hi" + Enter to the runner pane
 *   <space>ts              -> send paragraph under cursor to runner pane
 */

/* ---- Named-pane registry ------------------------------------------------ */

/* Direction of the split when a pane is first created or re-shown after
 * being parked in another window. BELOW = horizontal divider (tmux -v),
 * RIGHT = vertical divider (tmux -h, side-by-side). */
typedef enum {
    TMUX_SPLIT_BELOW = 0,
    TMUX_SPLIT_RIGHT,
} TmuxSplitDir;

/* Register a pane name with the command to spawn when it is created.
 * spawn_cmd may be NULL or "" for the default shell. dir controls which
 * side of the current pane the split appears on. Idempotent: calling
 * again with the same name updates the spawn command and direction.
 * Returns 1 on success, 0 if the registry is full or the name is invalid. */
int tmux_pane_register(const char *name, const char *spawn_cmd,
                       TmuxSplitDir dir);

/* Ensure a registered pane exists (create it if missing). */
int tmux_pane_ensure(const char *name);

/* Toggle a registered pane: create if missing, hide if visible in this
 * window, show (join into this window) if parked elsewhere. */
int tmux_pane_toggle(const char *name);

/* Kill a registered pane (if it currently exists). */
int tmux_pane_kill(const char *name);

/* Send a command line + Enter to a registered pane. Creates the pane
 * first if it does not exist. Does not change the focused pane. */
int tmux_pane_send(const char *name, const char *cmd);

/* Focus a registered pane: ensure it exists, ensure it is visible in
 * the current window (joining if parked elsewhere), then select-pane on
 * it. Updates the "last focused" pointer used by tmux_pane_send_focused. */
int tmux_pane_focus(const char *name);

/* Fill out[] with up to max registered pane names (interior pointers,
 * valid for the registry's lifetime). Returns the number written. */
int tmux_pane_list(const char **out, int max);

/* Attach an existing tmux pane (one not necessarily created by hed) to
 * a registered name. If the name is not yet registered, it is auto-
 * registered with no spawn command and TMUX_SPLIT_BELOW. Subsequent
 * focus/toggle/send calls operate on this pane_id. Returns 1 on success. */
int tmux_pane_attach(const char *name, const char *pane_id);

/* Name of the most recently focused/opened registered pane. Defaults
 * to "runner" before any focus has happened. Never NULL. */
const char *tmux_last_focused_pane(void);

/* Send to whichever pane is currently last_focused. */
int tmux_pane_send_focused(const char *cmd);

/* ---- Single-pane convenience API ---------------------------------------- */

/* Return non-zero if tmux integration is available (inside a tmux session). */
int tmux_is_available(void);

/* Equivalent to tmux_pane_ensure("runner"), etc. */
int tmux_ensure_pane(void);
int tmux_toggle_pane(void);
int tmux_kill_pane(void);
int tmux_send_command(const char *cmd);

/* tmux command history helpers (for :tmux_send browsing). Scoped to the
 * runner pane. */
void tmux_history_reset_browse(void);
int tmux_history_browse_up(const char *current_args, int current_len, char *out,
                           int out_cap);
int tmux_history_browse_down(char *out, int out_cap, int *restored);

#endif /* TMUX_H */
