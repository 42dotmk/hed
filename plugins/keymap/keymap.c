/* keymap plugin: runtime swap between vim/emacs/vscode keymaps.
 *
 * All keymap plugins must be loaded by the user's config (typically one
 * active and the others loaded-disabled), so this plugin can re-run
 * their init() to swap.
 *
 * Owns the :keymap and :keymap-toggle commands. */

#include "emacs_keybinds/emacs_keybinds.h"
#include "vim_keybinds/vim_keybinds.h"
#include "vscode_keybinds/vscode_keybinds.h"
#include "hed.h"

static const char *g_active = "vim";

static void apply(const char *name) {
    if (!name) return;
    if (strcmp(name, "vim") == 0) {
        if (plugin_vim_keybinds.init) plugin_vim_keybinds.init();
        ed_set_modeless(0);
        g_active = "vim";
    } else if (strcmp(name, "emacs") == 0) {
        if (plugin_emacs_keybinds.init) plugin_emacs_keybinds.init();
        ed_set_modeless(1);
        g_active = "emacs";
    } else if (strcmp(name, "vscode") == 0) {
        if (plugin_vscode_keybinds.init) plugin_vscode_keybinds.init();
        ed_set_modeless(1);
        g_active = "vscode";
    } else {
        ed_set_status_message("unknown keymap: %s (try vim|emacs|vscode)", name);
        return;
    }
    ed_set_status_message("keymap: %s", g_active);
}

static void cmd_keymap(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("keymap: %s", g_active);
        return;
    }
    apply(args);
}

/* Cycle: vim → emacs → vscode → vim ... */
static void cmd_keymap_toggle(const char *args) {
    (void)args;
    if      (strcmp(g_active, "vim")   == 0) apply("emacs");
    else if (strcmp(g_active, "emacs") == 0) apply("vscode");
    else                                     apply("vim");
}

static int keymap_init(void) {
    cmd("keymap",        cmd_keymap,        "switch keymap: keymap vim|emacs|vscode");
    cmd("keymap-toggle", cmd_keymap_toggle, "cycle keymap (vim → emacs → vscode)");
    return 0;
}

const Plugin plugin_keymap = {
    .name   = "keymap",
    .desc   = "runtime keymap swap (vim / emacs / vscode)",
    .init   = keymap_init,
    .deinit = NULL,
};
