#include "hook_builtins.h"
#include "editor.h"
#include "terminal.h"

void hook_change_cursor_shape(const HookModeEvent *event) {
    ed_change_cursor_shape();
    (void)event;
}
