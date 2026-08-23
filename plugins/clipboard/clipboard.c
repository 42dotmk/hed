/* clipboard plugin: mirror yank into the system clipboard via OSC 52. */

#include "hed.h"

static void osc52_copy(const char *data, size_t len) {
    if (!data || len == 0)
        return;

    static const char b64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t enc_len = 4 * ((len + 2) / 3);
    char *enc = malloc(enc_len + 1);
    if (!enc)
        return;
    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        unsigned a = (unsigned char)data[i];
        unsigned b = (i + 1 < len) ? (unsigned char)data[i + 1] : 0;
        unsigned c = (i + 2 < len) ? (unsigned char)data[i + 2] : 0;
        unsigned v = (a << 16) | (b << 8) | c;
        enc[o++] = b64[(v >> 18) & 0x3f];
        enc[o++] = b64[(v >> 12) & 0x3f];
        enc[o++] = (i + 1 < len) ? b64[(v >> 6) & 0x3f] : '=';
        enc[o++] = (i + 2 < len) ? b64[v & 0x3f] : '=';
    }
    enc[o] = '\0';

    /* Inside tmux, wrap with DCS passthrough so the outer terminal
     * sees the OSC. Requires `set -g set-clipboard on` in tmux.conf. */
    if (getenv("TMUX")) {
        fprintf(stderr, "\033Ptmux;\033\033]52;c;%s\033\033\\\033\\", enc);
    } else {
        fprintf(stderr, "\033]52;c;%s\007", enc);
    }
    fflush(stderr);
    free(enc);
}

/* HOOK_YANK fires after any yank lands in the registers — mirror the
 * unnamed register to the system clipboard. No keybinds: the keymaps
 * own y/yy, this plugin only observes. */
static void sync_unnamed(void) {
    const StrBuf *r = regs_get('"');
    if (r && r->data && r->len > 0)
        osc52_copy(r->data, r->len);
}

static int clipboard_init(void) {
    hook_register_simple(HOOK_YANK, sync_unnamed);
    return 0;
}

const Plugin plugin_clipboard = {
    .name = "clipboard",
    .desc = "mirror yank to system clipboard via OSC 52",
    .init = clipboard_init,
    .deinit = NULL,
};
