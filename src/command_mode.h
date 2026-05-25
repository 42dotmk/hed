#ifndef COMMAND_MODE_H
#define COMMAND_MODE_H

#include "prompt.h"

/*
 * The ":" prompt and the "/" search prompt.
 *
 * Both are PromptVTable instances; this header just exposes the entry
 * points used by keymaps, builtins and plugins.
 */

/* Open the ":" command prompt (same effect as pressing `:` in vim). */
void cmd_prompt_open(void);

/* Open the "/" interactive search prompt. */
void ed_search_prompt(void);

/*
 * Plugin hook: register a history navigation function for the ":"
 * prompt. Called on Up/Down arrow before the built-in command-history
 * fallback. Returns 1 if the hook handled the navigation (its caller
 * will skip later hooks), 0 to fall through to the next hook /
 * command-history. The hook may rewrite p->buf via prompt_set_text.
 *
 * Used by the tmux plugin to splice tmux-send history into the prompt
 * when the user is editing a `tmux_send ...` command.
 */
typedef int (*CmdPromptHistoryHook)(Prompt *p, int dir, void *ud);
void cmd_prompt_history_register(CmdPromptHistoryHook fn, void *ud);

/*
 * Plugin hook: register the fzf-based command-name picker used by the
 * ":" prompt's second Tab. The hook is called with the partial command
 * name typed so far. The picker is expected to refill the active prompt
 * (via prompt_set_text + prompt_keep_open).
 *
 * If no hook is registered, the second Tab is a no-op and the prompt
 * remains as-is. Used by the pickers plugin to provide cmd_cpick.
 */
typedef void (*CmdPromptCompletionPicker)(const char *query);
void cmd_prompt_completion_picker_register(CmdPromptCompletionPicker fn);

#endif /* COMMAND_MODE_H */
