#include "hook_builtins.h"
#include "hed.h"

void hook_change_cursor_shape(const HookModeEvent *event) {
    ed_change_cursor_shape();
    (void)event;
}
