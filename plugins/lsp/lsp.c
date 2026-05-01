/* lsp plugin: activates LSP integration.
 *
 * Owns the full lifecycle: lsp_init (server table reset), lsp_hooks_init
 * (buffer/keypress/mode hooks), and the :lsp_* command surface.
 *
 * Implementation lives next to this file in lsp_impl.c, lsp_hooks.c,
 * and cmd_lsp.c. */

#include "cmd_lsp.h"
#include "hed.h"
#include "lsp.h"
#include "lsp_hooks.h"

static int lsp_plugin_init(void) {
    lsp_init();
    lsp_hooks_init();

    cmd("lsp_connect",    cmd_lsp_connect,    "connect to a running LSP server");
    cmd("lsp_disconnect", cmd_lsp_disconnect, "disconnect LSP server");
    cmd("lsp_status",     cmd_lsp_status,     "show LSP server status");
    cmd("lsp_hover",      cmd_lsp_hover,      "LSP hover info");
    cmd("lsp_definition", cmd_lsp_definition, "LSP goto definition");
    cmd("lsp_completion", cmd_lsp_completion, "LSP completion");

    return 0;
}

const Plugin plugin_lsp = {
    .name   = "lsp",
    .desc   = "LSP client integration",
    .init   = lsp_plugin_init,
    .deinit = NULL,
};
