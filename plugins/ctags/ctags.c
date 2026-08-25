/* ctags plugin: :tag lookup driven by utils/ctags.c, plus the
 * :definition dispatcher that `gd` binds to. */

#include "ctags/tags.h"
#include "hed.h"

static void cmd_tag(const char *args) {
    goto_tag(args && *args ? args : NULL);
    buf_center_screen();
}

/* Weak probe into the lsp plugin: absent (or -1) when no ready server
 * is attached to the current buffer, in which case ctags handles the
 * jump. Weak linkage keeps this plugin standalone under PLUGINS_DIR
 * swaps that drop lsp (same pattern as treesitter/theme.h). */
int lsp_definition_try(void) __attribute__((weak));

static void cmd_definition(const char *args) {
    if (&lsp_definition_try && lsp_definition_try() == 0)
        return;
    cmd_tag(args);
}

static int ctags_init(void) {
    cmd("tag", cmd_tag, "jump to tag definition");
    cmd("definition", cmd_definition, "goto definition (LSP, ctags fallback)");
    return 0;
}

const Plugin plugin_ctags = {
    .name = "ctags",
    .desc = "ctags lookup (:tag)",
    .init = ctags_init,
    .deinit = NULL,
};
