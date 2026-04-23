#include "hed.h"
#include "lsp.h"

static void lsp_hook_buffer_open(const HookBufferEvent *event) {
    if (event) lsp_on_buffer_open(event->buf);
}

static void lsp_hook_buffer_close(const HookBufferEvent *event) {
    if (event) lsp_on_buffer_close(event->buf);
}

static void lsp_hook_buffer_save(const HookBufferEvent *event) {
    if (event) lsp_on_buffer_save(event->buf);
}

/* Send didChange when leaving INSERT mode — batches all edits into one sync. */
static void lsp_hook_mode_change(const HookModeEvent *event) {
    if (!event) return;
    if (event->old_mode == MODE_INSERT && event->new_mode == MODE_NORMAL) {
        Buffer *buf = buf_cur();
        if (buf) lsp_on_buffer_changed(buf);
    }
}

void lsp_hooks_init(void) {
    hook_register_buffer(HOOK_BUFFER_OPEN,  MODE_NORMAL, "*", lsp_hook_buffer_open);
    hook_register_buffer(HOOK_BUFFER_CLOSE, MODE_NORMAL, "*", lsp_hook_buffer_close);
    hook_register_buffer(HOOK_BUFFER_SAVE,  MODE_NORMAL, "*", lsp_hook_buffer_save);
    hook_register_mode(HOOK_MODE_CHANGE, lsp_hook_mode_change);
}
