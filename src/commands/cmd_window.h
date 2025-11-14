#ifndef CMD_WINDOW_H
#define CMD_WINDOW_H

/*
 * WINDOW COMMAND MODULE
 * =====================
 *
 * Commands for managing editor window splits and focus.
 *
 * Commands:
 * - :split    - Split current window horizontally
 * - :vsplit   - Split current window vertically
 * - :wfocus   - Focus next window
 * - :wclose   - Close current window
 */

void cmd_split(const char *args);
void cmd_vsplit(const char *args);
void cmd_wfocus(const char *args);
void cmd_wclose(const char *args);

#endif /* CMD_WINDOW_H */
