/* dired plugin: activates the directory browser.
 *
 * Hooks into HOOK_BUFFER_OPEN_PRE / HOOK_BUFFER_SAVE_PRE so core never
 * mentions dired. The implementation lives in dired_impl.c. */

#include "../plugin.h"
#include "hed.h"
#include "dired.h"
#include "file_helpers.h"

static void on_enter(void)  { dired_handle_enter(); }
static void on_parent(void) { dired_handle_parent(); }
static void on_home(void)   { dired_handle_home(); }
static void on_chdir(void)  { dired_handle_chdir(); }

/* Intercept "open this path" if it's a directory. */
static void dired_open_pre(HookBufferEvent *ev) {
    if (!ev || !ev->filename) return;
    if (path_is_dir(ev->filename)) {
        dired_open(ev->filename);
        ev->consumed = 1;
    }
}

/* Intercept "save this buffer" if it's a dired buffer (commits the rename
 * /create/delete plan instead of writing file bytes). */
static void dired_save_pre(HookBufferEvent *ev) {
    if (!ev || !ev->buf) return;
    if (dired_handle_save(ev->buf))
        ev->consumed = 1;
}

static int dired_plugin_init(void) {
    dired_hooks_init();
    hook_register_buffer(HOOK_BUFFER_OPEN_PRE, MODE_NORMAL, "*", dired_open_pre);
    hook_register_buffer(HOOK_BUFFER_SAVE_PRE, MODE_NORMAL, "*", dired_save_pre);
    mapn("<CR>", on_enter,  "dired open");
    mapn("-",    on_parent, "dired parent");
    mapn("~",    on_home,   "dired home");
    mapn("cd",   on_chdir,  "dired chdir");
    return 0;
}

const Plugin plugin_dired = {
    .name   = "dired",
    .desc   = "directory browser (oil.nvim-like)",
    .init   = dired_plugin_init,
    .deinit = NULL,
};
