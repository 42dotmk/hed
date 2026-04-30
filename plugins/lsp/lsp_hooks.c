#include "hed.h"
#include "lsp.h"
#include "winmodal.h"

static void lsp_hook_buffer_open(HookBufferEvent *event) {
    if (event) lsp_on_buffer_open(event->buf);
}

static void lsp_hook_buffer_close(HookBufferEvent *event) {
    if (event) lsp_on_buffer_close(event->buf);
}

static void lsp_hook_buffer_save(HookBufferEvent *event) {
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

/* Dismiss or scroll a read-only popup modal on keypress. */
static void lsp_hook_keypress(HookKeyEvent *event) {
    Window *modal = winmodal_current();
    if (!modal) return;

    int     idx  = modal->buffer_index;
    Buffer *mbuf = (idx >= 0 && idx < (int)E.buffers.len)
                   ? &E.buffers.data[idx] : NULL;
    if (!mbuf || !mbuf->readonly) return;

    int key = event->key;
    if (key == '\x1b' || key == 'q') {
        winmodal_destroy(modal);
        mbuf->dirty = 0;
        buf_close(idx);
    } else if (key == 'j' || key == KEY_ARROW_DOWN) {
        int max_off = mbuf->num_rows - modal->height;
        if (max_off < 0) max_off = 0;
        if (modal->row_offset < max_off) modal->row_offset++;
    } else if (key == 'k' || key == KEY_ARROW_UP) {
        if (modal->row_offset > 0) modal->row_offset--;
    }
    event->consumed = 1; /* swallow all keys while popup is open */
}

void lsp_hooks_init(void) {
    hook_register_buffer(HOOK_BUFFER_OPEN,  MODE_NORMAL, "*", lsp_hook_buffer_open);
    hook_register_buffer(HOOK_BUFFER_CLOSE, MODE_NORMAL, "*", lsp_hook_buffer_close);
    hook_register_buffer(HOOK_BUFFER_SAVE,  MODE_NORMAL, "*", lsp_hook_buffer_save);
    hook_register_mode(HOOK_MODE_CHANGE, lsp_hook_mode_change);
    hook_register_key(HOOK_KEYPRESS, lsp_hook_keypress);
}
