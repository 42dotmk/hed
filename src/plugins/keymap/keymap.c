/* keymap plugin: runtime swap between vim_keybinds and emacs_keybinds.
 *
 * Owns:
 *   - the g_active state
 *   - :keymap <name> and :keymap-toggle commands
 *
 * Both keymap plugins must be loaded by the user's config (typically:
 *   plugin_load(&plugin_vim_keybinds,   1);
 *   plugin_load(&plugin_emacs_keybinds, 0);
 * ), so this plugin can re-run their init() to swap. */

#include "../plugin.h"
#include "../emacs_keybinds/emacs_keybinds.h"
#include "../vim_keybinds/vim_keybinds.h"
#include "hed.h"
#include <string.h>

static const char *g_active = "vim";

static void apply(const char *name) {
    if (!name) return;
    if (strcmp(name, "emacs") == 0) {
        if (plugin_emacs_keybinds.init) plugin_emacs_keybinds.init();
        emacs_keybinds_set_modeless(1);
        g_active = "emacs";
    } else if (strcmp(name, "vim") == 0) {
        if (plugin_vim_keybinds.init) plugin_vim_keybinds.init();
        emacs_keybinds_set_modeless(0);
        g_active = "vim";
    } else {
        ed_set_status_message("unknown keymap: %s (try vim|emacs)", name);
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

static void cmd_keymap_toggle(const char *args) {
    (void)args;
    apply(strcmp(g_active, "vim") == 0 ? "emacs" : "vim");
}

static int keymap_init(void) {
    cmd("keymap",        cmd_keymap,        "switch keymap: keymap vim|emacs");
    cmd("keymap-toggle", cmd_keymap_toggle, "toggle vim/emacs keymap");
    return 0;
}

const Plugin plugin_keymap = {
    .name   = "keymap",
    .desc   = "runtime keymap swap (vim ↔ emacs)",
    .init   = keymap_init,
    .deinit = NULL,
};
