/* session plugin: persists and restores the open-buffer list.
 *
 * Two surfaces:
 *   - automatic: on startup, if HED_RELOAD is set (the reload plugin's
 *     contract), restore from ~/.cache/hed/<encoded-cwd>/session and
 *     remove the file.
 *   - manual:    :session-save / :session-restore use the same cache
 *     path. Useful for ad-hoc workspace snapshots.
 *
 * The save/restore mechanic itself lives next door in session.{c,h}
 * so any feature (autosave-on-quit, named sessions, …) can reuse it. */

#include "hed.h"
#include "session.h"

static int session_default_path(char *out, size_t cap) {
    return path_cache_file_for_cwd("session", out, cap) ? 1 : 0;
}

static void cmd_session_save(const char *args) {
    (void)args;
    char path[4096];
    if (!session_default_path(path, sizeof(path))) {
        ed_set_status_message("session: could not resolve cache path");
        return;
    }
    if (session_save(path) != ED_OK) {
        ed_set_status_message("session: save failed (%s)", path);
        return;
    }
    ed_set_status_message("session: saved to %s", path);
}

static void cmd_session_restore(const char *args) {
    (void)args;
    char path[4096];
    if (!session_default_path(path, sizeof(path))) {
        ed_set_status_message("session: could not resolve cache path");
        return;
    }
    if (session_restore(path) != ED_OK) {
        ed_set_status_message("session: nothing to restore (%s)", path);
        return;
    }
    ed_set_status_message("session: restored from %s", path);
}

static void on_startup_done(void) {
    if (!getenv("HED_RELOAD")) return;
    unsetenv("HED_RELOAD");

    char path[4096];
    if (!session_default_path(path, sizeof(path))) return;

    if (session_restore(path) == ED_OK) {
        unlink(path);
    }
}

static int session_init(void) {
    cmd("session-save",    cmd_session_save,    "save open buffers to cache");
    cmd("session-restore", cmd_session_restore, "restore open buffers from cache");
    hook_register_simple(HOOK_STARTUP_DONE, on_startup_done);
    return 0;
}

const Plugin plugin_session = {
    .name   = "session",
    .desc   = "save/restore the open-buffer list; auto-restores after :reload",
    .init   = session_init,
    .deinit = NULL,
};
