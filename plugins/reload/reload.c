/* reload plugin: rebuilds hed and replaces the running process with
 * the new binary. Hands the open-buffer list off to the `session`
 * plugin via the cache file + HED_RELOAD env var contract.
 *
 * Restoring is the session plugin's job — this one only saves and
 * execs. */

#include "hed.h"
#include "session/session.h"

static void cmd_reload(const char *args) {
    (void)args;

    int status = term_cmd_run_interactive("make -j16", true);
    if (status != 0) {
        ed_set_status_message("reload: build failed (status %d)", status);
        return;
    }

    char path[4096];
    if (path_cache_file_for_cwd("session", path, sizeof(path))) {
        session_save(path);
        setenv("HED_RELOAD", "1", 1);
    }

    disable_raw_mode();

    const char *exe = "./build/hed";
    execl(exe, exe, (char *)NULL);

    /* exec failed */
    perror("execl");
    enable_raw_mode();
    ed_set_status_message("reload: failed to exec %s", exe);
}

static int reload_init(void) {
    cmd("reload", cmd_reload, "rebuild+restart hed");
    return 0;
}

const Plugin plugin_reload = {
    .name   = "reload",
    .desc   = "rebuild and restart hed in place; hands open buffers to session",
    .init   = reload_init,
    .deinit = NULL,
};
