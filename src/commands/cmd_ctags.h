#ifndef CMD_CTAGS_H
#define CMD_CTAGS_H
/*
 * CTAGS COMMANDS
 * ==============
 *
 * Commands:
 * - :tag [symbol]  - Jump to a ctags definition (default: word under cursor)
 */

void cmd_tag(const char *args);

#endif /* CMD_CTAGS_H */
