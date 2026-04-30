/* example plugin: a self-explanatory baseline. Copy this directory,
 * rename the symbol/files, and you have a new plugin.
 *
 * What this shows:
 *   - the minimum surface (init function, Plugin descriptor)
 *   - how to register a command
 *   - how to register a normal-mode keybind
 *   - how to register a hook
 *
 * See README.md for the step-by-step recipe. */

#include "plugin.h"
#include "hed.h"

/* --- a command --- */

static void cmd_example(const char *args) {
    if (args && *args) {
        ed_set_status_message("hello, %s!", args);
    } else {
        ed_set_status_message("hello from the example plugin");
    }
}

/* --- a keybind callback --- */

static void kb_example(void) {
    ed_set_status_message("example keybind fired");
}

/* --- a hook (logs every mode change to .hedlog) --- */

static void on_mode_change(const HookModeEvent *e) {
    if (!e) return;
    log_msg("example: mode %d -> %d", e->old_mode, e->new_mode);
}

/* --- plugin lifecycle --- */

static int example_init(void) {
    cmd("hello", cmd_example, "say hello (example plugin)");
    mapn(" eh", kb_example, "example: hello");        /* leader: <space>eh */
    hook_register_mode(HOOK_MODE_CHANGE, on_mode_change);
    return 0;
}

const Plugin plugin_example = {
    .name   = "example",
    .desc   = "starter template — copy this directory to make your own",
    .init   = example_init,
    .deinit = NULL,
};
