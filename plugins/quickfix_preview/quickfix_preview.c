/* quickfix_preview plugin: keep E.qf.sel and the preview window in sync
 * with the cursor row when the quickfix buffer is focused. */

#include "plugin.h"
#include "hed.h"

static void cursor_hook(const HookCursorEvent *event) {
    if (!event || !event->buf) return;
    Buffer *buf = event->buf;
    if (!buf->filetype || strcmp(buf->filetype, "quickfix") != 0) return;

    if (E.qf.items.len == 0) return;

    int row = event->new_y;
    if (row < 0) row = 0;
    if (row >= (int)E.qf.items.len) row = (int)E.qf.items.len - 1;

    E.qf.sel = row;
    qf_update_view(&E.qf);
    qf_preview_selected(&E.qf);
}

static int quickfix_preview_init(void) {
    hook_register_cursor(HOOK_CURSOR_MOVE, MODE_NORMAL, "quickfix", cursor_hook);
    return 0;
}

const Plugin plugin_quickfix_preview = {
    .name   = "quickfix_preview",
    .desc   = "live preview of quickfix entry under cursor",
    .init   = quickfix_preview_init,
    .deinit = NULL,
};
