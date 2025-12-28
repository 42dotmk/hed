#include "hed.h"
#include "lsp.h"

/* Hook callbacks to integrate LSP with buffer lifecycle */

static void lsp_hook_buffer_open(const HookBufferEvent *event) {
    if (!event || !event->buf)
        return;

    lsp_on_buffer_open(event->buf);
}

static void lsp_hook_buffer_close(const HookBufferEvent *event) {
    if (!event || !event->buf)
        return;

    lsp_on_buffer_close(event->buf);
}

static void lsp_hook_buffer_save(const HookBufferEvent *event) {
    if (!event || !event->buf)
        return;

    lsp_on_buffer_save(event->buf);
}

/* Register LSP hooks - call this from user_hooks_init() in config.c */
void lsp_hooks_init(void) {
    /* Register for all modes and all filetypes */
    hook_register_buffer(HOOK_BUFFER_OPEN, MODE_NORMAL, "*", lsp_hook_buffer_open);
    hook_register_buffer(HOOK_BUFFER_CLOSE, MODE_NORMAL, "*", lsp_hook_buffer_close);
    hook_register_buffer(HOOK_BUFFER_SAVE, MODE_NORMAL, "*", lsp_hook_buffer_save);
}
