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

#include "hed.h"
void hook_smart_indent(const HookCharEvent *event);
void hook_change_cursor_shape(const HookModeEvent *event);
void hook_auto_pair(const HookCharEvent *event);

#endif // !HOOK_BUILTINS_H
