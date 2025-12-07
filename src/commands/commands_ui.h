#ifndef COMMANDS_UI_H
#define COMMANDS_UI_H

/*
 * UI COMMAND MODULE
 * =================
 *
 * Consolidated window management and quickfix commands.
 * Combines cmd_window.h and cmd_quickfix.h.
 *
 * Window Management Commands:
 * - :split    - Split current window horizontally
 * - :vsplit   - Split current window vertically
 * - :wfocus   - Focus next window
 * - :wclose   - Close current window
 * - :new      - Create new split with new empty buffer
 *
 * Quickfix Commands:
 * - :copen [height]  - Open quickfix window
 * - :cclose          - Close quickfix window
 * - :ctoggle [height]- Toggle quickfix window
 * - :cclear          - Clear quickfix list
 * - :cadd [entry]    - Add entry to quickfix
 * - :cnext           - Go to next quickfix entry
 * - :cprev           - Go to previous quickfix entry
 * - :copenidx <N>    - Open quickfix entry at index N
 */

/* Window management */
void cmd_split(const char *args);
void cmd_vsplit(const char *args);
void cmd_wfocus(const char *args);
void cmd_wclose(const char *args);
void cmd_new(const char *args);
void cmd_wleft(const char *args);
void cmd_wright(const char *args);
void cmd_wup(const char *args);
void cmd_wdown(const char *args);

/* Quickfix */
void cmd_copen(const char *args);
void cmd_cclose(const char *args);
void cmd_ctoggle(const char *args);
void cmd_cclear(const char *args);
void cmd_cadd(const char *args);
void cmd_cnext(const char *args);
void cmd_cprev(const char *args);
void cmd_copenidx(const char *args);

#endif /* COMMANDS_UI_H */
