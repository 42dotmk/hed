#ifndef HED_PLUGIN_AISHELL_H
#define HED_PLUGIN_AISHELL_H

#include "plugin.h"

extern const Plugin plugin_aishell;

/* Override the default command executed when the AI shell pane is spawned.
 * Pass any command available on $PATH, e.g. "claude", "copilot", "aider", etc.
 * Call this from config_init() before plugin_load(&plugin_aishell, ...). */
void aishell_set_spawn_cmd(const char *cmd);

#endif
