#ifndef CMD_BUILTINS_H
#define CMD_BUILTINS_H

/*
 * BUILT-IN COMMAND AGGREGATOR
 * ===========================
 *
 * Convenience header that exposes all built-in command declarations in one
 * place. Include this from modules that need to call or register the built-in
 * commands (e.g., config and keybind wiring).
 */

#include "cmd_misc.h"
#include "cmd_ctags.h"
#include "cmd_search.h"
#include "commands_buffer.h"
#include "commands_ui.h"

#endif /* CMD_BUILTINS_H */
