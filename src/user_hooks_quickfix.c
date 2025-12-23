#include "hed.h"

/* Quickfix cursor hook: keep E.qf.sel in sync with the cursor row
 * when the quickfix buffer (filetype="quickfix") is focused. */
static void quickfix_cursor_hook(const HookCursorEvent *event) {
    if (!event || !event->buf)
        return;
    Buffer *buf = event->buf;
    if (!buf->filetype || strcmp(buf->filetype, "quickfix") != 0)
        return;

    if (E.qf.items.len == 0)
        return;

    int row = event->new_y;
    if (row < 0)
        row = 0;
    if (row >= (int)E.qf.items.len)
        row = (int)E.qf.items.len - 1;

    E.qf.sel = row;
    /* Keep the quickfix view and '*' marker in sync with selection. */
    qf_update_view(&E.qf);
    /* Preview the selected item in the target window but keep focus
     * and cursor in the quickfix pane while navigating with j/k. */
    qf_preview_selected(&E.qf);
}

void user_hooks_quickfix_init(void) {
    /* Track cursor movement in quickfix buffer in normal mode */
    hook_register_cursor(HOOK_CURSOR_MOVE, MODE_NORMAL, "quickfix",
                         quickfix_cursor_hook);
}
