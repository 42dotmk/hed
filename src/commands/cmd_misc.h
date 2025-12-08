#ifndef CMD_MISC_H
#define CMD_MISC_H

/*
 * MISCELLANEOUS COMMAND MODULE
 * =============================
 *
 * Miscellaneous editor commands that don't fit other categories.
 *
 * Commands:
 * - :list, :ls     - List all available commands
 * - :echo <text>   - Print text to status bar (supports escape sequences)
 * - :history [N]   - Show command history (last N entries, default 20)
 * - :registers     - Show register contents
 * - :put [reg]     - Paste from register below cursor
 * - :undo, :u      - Undo last change
 * - :redo, :r      - Redo last undone change
 * - :ln            - Toggle line numbers
 * - :rln           - Toggle relative line numbers
 * - :logclear      - Clear debug log
 * - :ts <mode>     - Control tree-sitter (on|off|auto)
 * - :tslang <name> - Set tree-sitter language for current buffer
 * - :o             - Insert new line below and enter insert mode
 * - :O             - Insert new line above and enter insert mode
 * - :shell <cmd>   - Run shell command interactively (with terminal output)
 * - :git           - Run lazygit (via :shell)
 * - :wrap          - Toggle soft-wrap for current window
 * - :wrapdefault   - Toggle default soft-wrap for new windows
 * - :reload        - Rebuild and restart hed (hot reload)
 * - :fmt           - Format current buffer via external tool
 * - :tmux_toggle   - Toggle dedicated tmux runner pane
 * - :tmux_send     - Send a command line to tmux runner pane
 * - :tmux_kill     - Kill the dedicated tmux runner pane
 */

void cmd_list_commands(const char *args);
void cmd_echo(const char *args);
void cmd_history(const char *args);
void cmd_registers(const char *args);
void cmd_put(const char *args);
void cmd_undo(const char *args);
void cmd_redo(const char *args);
void cmd_ln(const char *args);
void cmd_rln(const char *args);
void cmd_logclear(const char *args);
void cmd_ts(const char *args);
void cmd_tslang(const char *args);
void cmd_tsi(const char *args);
void cmd_new_line(const char *args);
void cmd_new_line_above(const char *args);
void cmd_shell(const char *args);
void cmd_git(const char *args);
void cmd_wrap(const char *args);
void cmd_wrapdefault(const char *args);
void cmd_reload(const char *args);
void cmd_fmt(const char *args);
void cmd_tmux_toggle(const char *args);
void cmd_tmux_send(const char *args);
void cmd_tmux_kill(const char *args);

#endif /* CMD_MISC_H */
