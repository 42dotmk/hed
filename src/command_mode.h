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
 * The colon prompt's second Tab hands off to the picker registered
 * under name "command" (see src/picker.h). The pickers plugin wires
 * this to its fzf-backed command palette. Other prompts that want
 * picker support follow the same pattern: pick a name, register an
 * implementation, call picker_invoke from the prompt's complete().
 */

#endif /* COMMAND_MODE_H */
