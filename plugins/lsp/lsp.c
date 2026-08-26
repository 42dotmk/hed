/* lsp plugin: activates LSP integration.
 *
 * Owns the full lifecycle: lsp_init (server table reset), lsp_hooks_init
 * (buffer/keypress/mode hooks), and the :lsp_* command surface.
 *
 * Implementation lives next to this file in lsp_impl.c, lsp_hooks.c,
 * and cmd_lsp.c. */

#include "lsp.h"
#include "cmd_lsp.h"
#include "hed.h"
#include "lsp_hooks.h"

static void lsp_plugin_deinit(void) { lsp_shutdown(); }

static int lsp_plugin_init(void) {
    lsp_init();
    lsp_hooks_init();
    lsp_completion_source_register();

    /* cmd_quit exits the process directly, so plugin deinit never runs
     * there — atexit covers it. lsp_shutdown is idempotent. */
    atexit(lsp_shutdown);

    cmd("lsp_start", cmd_lsp_start,
        "spawn an LSP server (auto-detects from filetype)");
    cmd("lsp_connect", cmd_lsp_connect, "connect to a running LSP server");
    cmd("lsp_disconnect", cmd_lsp_disconnect, "disconnect LSP server");
    cmd("lsp_status", cmd_lsp_status, "show LSP server status");
    cmd("lsp_autostart", cmd_lsp_autostart,
        "auto-start servers on buffer open (default off)");
    cmd("lsp_hover", cmd_lsp_hover, "LSP hover info");
    cmd("lsp_definition", cmd_lsp_definition, "LSP goto definition");
    cmd("lsp_diagnostics", cmd_lsp_diagnostics,
        "list LSP diagnostics in quickfix");

    return 0;
}

const Plugin plugin_lsp = {
    .name = "lsp",
    .desc = "LSP client integration",
    .init = lsp_plugin_init,
    .deinit = lsp_plugin_deinit,
};
