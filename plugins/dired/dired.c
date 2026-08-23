/* dired plugin: activates the directory browser.
 *
 * Hooks into HOOK_BUFFER_OPEN_PRE / HOOK_BUFFER_SAVE_PRE so core never
 * mentions dired. The implementation lives in dired_impl.c. */

#include "dired.h"
#include "hed.h"

/* Filetype-scoped commands: only resolve inside a dired buffer, and
 * the keys are plain cmaps onto them. */
static void cmd_dired_open(const char *args) {
    (void)args;
    dired_handle_enter();
}
static void cmd_dired_up(const char *args) {
    (void)args;
    dired_handle_parent();
}
static void cmd_dired_home(const char *args) {
    (void)args;
    dired_handle_home();
}
static void cmd_dired_cd(const char *args) {
    (void)args;
    dired_handle_chdir();
}

/* Intercept "open this path" if it's a directory. */
static void dired_open_pre(HookBufferEvent *ev) {
    if (!ev || !ev->filename)
        return;
    if (fs_is_dir(ev->filename)) {
        dired_open(ev->filename);
        ev->consumed = 1;
    }
}

/* Intercept "save this buffer" if it's a dired buffer (commits the rename
 * /create/delete plan instead of writing file bytes). */
static void dired_save_pre(HookBufferEvent *ev) {
    if (!ev || !ev->buf)
        return;
    if (dired_handle_save(ev->buf))
        ev->consumed = 1;
}

static int dired_plugin_init(void) {
    dired_hooks_init();
    hook_register_buffer(HOOK_BUFFER_OPEN_PRE, MODE_NORMAL, "*",
                         dired_open_pre);
    hook_register_buffer(HOOK_BUFFER_SAVE_PRE, MODE_NORMAL, "*",
                         dired_save_pre);
    cmd_ft("dired", "dired-open", cmd_dired_open, "open entry under cursor");
    cmd_ft("dired", "dired-up", cmd_dired_up, "go to parent directory");
    cmd_ft("dired", "dired-home", cmd_dired_home, "go to home directory");
    cmd_ft("dired", "dired-cd", cmd_dired_cd, "chdir to this directory");
    cmapn_ft("dired", "<CR>", "dired-open", "dired open");
    cmapn_ft("dired", "-", "dired-up", "dired parent");
    cmapn_ft("dired", "~", "dired-home", "dired home");
    cmapn_ft("dired", "cd", "dired-cd", "dired chdir");
    return 0;
}

const Plugin plugin_dired = {
    .name = "dired",
    .desc = "directory browser (oil.nvim-like)",
    .init = dired_plugin_init,
    .deinit = NULL,
};
