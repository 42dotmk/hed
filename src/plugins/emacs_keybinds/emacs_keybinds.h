#ifndef HED_EMACS_KEYBINDS_H
#define HED_EMACS_KEYBINDS_H

#include "../plugin.h"

extern const Plugin plugin_emacs_keybinds;

/* Toggle the always-insert mode-change hook installed by emacs_keybinds.
 * Pass 1 to make hed effectively modeless (Emacs feel), 0 to restore
 * normal modal behavior. The hook itself is always registered while the
 * plugin is enabled — this just gates whether it fires.
 *
 * Useful for runtime keymap switching: when swapping back to a modal
 * keymap (e.g., vim_keybinds), call emacs_keybinds_set_modeless(0). */
void emacs_keybinds_set_modeless(int on);

/* Returns the current setting (0 or 1). */
int emacs_keybinds_is_modeless(void);

#endif
