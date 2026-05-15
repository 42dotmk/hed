#ifndef HED_PLUGIN_SHELL_H
#define HED_PLUGIN_SHELL_H

#include "plugin.h"

extern const Plugin plugin_shell;

/* Run an already-formed shell command line. Args may include the
 * `--skipwait` flag and the trailing `>%b` / `>>%b` / `>%v` / `>%y`
 * capture tokens; %p / %d / %n / %b / %y in the body are expanded
 * against the current buffer. Public so other plugins (e.g. treesitter
 * for :tsi) can shell out without re-parsing through the colon
 * command. With NULL or empty args, opens the interactive `!` prompt.
 */
void cmd_shell(const char *args);

/* Open the `!` prompt pre-filled with `text` (clamped to the prompt
 * buffer size). Pass len < 0 to use strlen(text). NULL/empty text
 * opens an empty prompt (same as `cmd_shell(NULL)`). */
void shell_open_prompt(const char *text, int len);

#endif /* HED_PLUGIN_SHELL_H */
