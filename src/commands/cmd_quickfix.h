#ifndef CMD_QUICKFIX_H
#define CMD_QUICKFIX_H

/*
 * QUICKFIX COMMAND MODULE
 * =======================
 *
 * Vim-style quickfix list commands for error navigation and search results.
 *
 * Commands:
 * - :copen [height]  - Open quickfix window
 * - :cclose          - Close quickfix window
 * - :ctoggle [height]- Toggle quickfix window
 * - :cclear          - Clear quickfix list
 * - :cadd [entry]    - Add entry to quickfix
 * - :cnext           - Go to next quickfix entry
 * - :cprev           - Go to previous quickfix entry
 * - :copenidx <N>    - Open quickfix entry at index N
 */

void cmd_copen(const char *args);
void cmd_cclose(const char *args);
void cmd_ctoggle(const char *args);
void cmd_cclear(const char *args);
void cmd_cadd(const char *args);
void cmd_cnext(const char *args);
void cmd_cprev(const char *args);
void cmd_copenidx(const char *args);

#endif /* CMD_QUICKFIX_H */
