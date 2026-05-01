#ifndef HOOK_BUILTINS_H
#define HOOK_BUILTINS_H
/*
 * BUILT-IN HOOK CALLBACKS
 * =======================
 *
 * Convenience header that exposes the default hook actions.
 * Include this alongside hooks.h when you need to bind or call
 * the built-in behaviors (e.g., config and editor modules).
 */

#include "hooks.h"
void hook_change_cursor_shape(const HookModeEvent *event);

#endif // !HOOK_BUILTINS_H
