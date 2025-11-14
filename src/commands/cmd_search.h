#ifndef CMD_SEARCH_H
#define CMD_SEARCH_H

/*
 * SEARCH AND TOOL COMMAND MODULE
 * ===============================
 *
 * Commands for searching, file navigation, and external tool integration.
 *
 * Commands:
 * - :cpick         - Interactive command picker with fzf
 * - :ssearch       - Search current file with ripgrep/fzf
 * - :rg [pattern]  - Ripgrep search with live results
 * - :fzf           - Interactive file picker
 * - :recent        - Pick from recently opened files
 * - :shq <cmd>     - Run shell command and capture to quickfix
 */

void cmd_cpick(const char *args);
void cmd_ssearch(const char *args);
void cmd_rg(const char *args);
void cmd_fzf(const char *args);
void cmd_recent(const char *args);
void cmd_shq(const char *args);

#endif /* CMD_SEARCH_H */
