#include "lsp_hooks.h"
#include "hed.h"
#include "lsp.h"

/* The popup modal we own. Set by lsp_popup_track() right after the
 * modal is shown; cleared when we tear it down. */
static Window *lsp_popup = NULL;

void lsp_popup_track(Window *modal) { lsp_popup = modal; }

static void lsp_hook_buffer_open(HookBufferEvent *event) {
    if (event)
        lsp_on_buffer_open(event->buf);
}

static void lsp_hook_buffer_close(HookBufferEvent *event) {
    if (event)
        lsp_on_buffer_close(event->buf);
}

static void lsp_hook_buffer_save(HookBufferEvent *event) {
    if (event)
        lsp_on_buffer_save(event->buf);
}

/* Sync when leaving INSERT mode — batches all edits into one didChange
 * (requests made mid-insert sync on demand via lsp_sync_document). */
static void lsp_hook_mode_change(const HookModeEvent *event) {
    if (!event)
        return;
    if (event->old_mode == MODE_INSERT && event->new_mode == MODE_NORMAL) {
        Buffer *buf = buf_cur();
        if (buf)
            lsp_sync_document(buf);
    }
}

/* Dismiss or scroll a read-only popup modal on keypress. */
static void lsp_hook_keypress(HookKeyEvent *event) {
    Window *modal = winmodal_current();
    if (!modal || modal != lsp_popup)
        return;

    if (buf_special_modal_key(modal, event->key, 1) == 2)
        lsp_popup = NULL;
    event->consumed = 1; /* swallow all keys while popup is open */
}

void lsp_hooks_init(void) {
    hook_register_buffer(HOOK_BUFFER_OPEN, MODE_NORMAL, "*",
                         lsp_hook_buffer_open);
    hook_register_buffer(HOOK_BUFFER_CLOSE, MODE_NORMAL, "*",
                         lsp_hook_buffer_close);
    hook_register_buffer(HOOK_BUFFER_SAVE, MODE_NORMAL, "*",
                         lsp_hook_buffer_save);
    hook_register_mode(HOOK_MODE_CHANGE, lsp_hook_mode_change);
    hook_register_key(HOOK_KEYPRESS, lsp_hook_keypress);
}
