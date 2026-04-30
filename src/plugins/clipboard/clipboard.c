/* clipboard plugin: mirror yank into the system clipboard via OSC 52. */

#include "../plugin.h"
#include "hed.h"
#include "keybinds_builtins.h"
#include "registers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void osc52_copy(const char *data, size_t len) {
    if (!data || len == 0) return;

    static const char b64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t enc_len = 4 * ((len + 2) / 3);
    char *enc = malloc(enc_len + 1);
    if (!enc) return;
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

static void sync_unnamed(void) {
    const SizedStr *r = regs_get('"');
    if (r && r->data && r->len > 0) osc52_copy(r->data, r->len);
}

static void kb_yank_line_clip(void) {
    kb_yank_line();
    sync_unnamed();
}

static void kb_operator_yank_clip(void) {
    kb_operator_yank();
    sync_unnamed();
}

static void kb_visual_yank_selection_clip(void) {
    kb_visual_yank_selection();
    sync_unnamed();
}

static int clipboard_init(void) {
    mapv("y",  kb_visual_yank_selection_clip, "yank (+clipboard)");
    mapn("y",  kb_operator_yank_clip,         "yank operator (+clipboard)");
    mapn("yy", kb_yank_line_clip,             "yank line (+clipboard)");
    return 0;
}

const Plugin plugin_clipboard = {
    .name   = "clipboard",
    .desc   = "mirror yank to system clipboard via OSC 52",
    .init   = clipboard_init,
    .deinit = NULL,
};
