/* yazi plugin: launch the yazi file manager as a chooser.
 *
 * Flow:
 *   1. Build a temp chooser-file path under $TMPDIR (fallback /tmp).
 *   2. Run `yazi --chooser-file=<tmp> [start_dir]` via the existing TUI
 *      handoff helper (term_cmd_run_interactive), which suspends raw
 *      mode while yazi owns the terminal.
 *   3. After yazi exits, read the chooser file. Yazi writes one
 *      absolute path per selected entry (newline-terminated). Open
 *      every non-empty line in hed.
 *   4. Unlink the temp file.
 *
 * Optional argument to `:yazi`: a starting path passed as yazi's
 * positional arg. Shell-quoted so spaces are safe. */

#include "hed.h"
#include "yazi.h"

/* If `path` exists and contains a slash, write its parent dir into
 * out[0..cap). Returns 1 on success (out filled), 0 otherwise. */
static int dirname_if_exists(const char *path, char *out, size_t cap) {
    if (!path || !*path) return 0;
    if (!fs_exists(path)) return 0;
    const char *slash = strrchr(path, '/');
    if (!slash) {
        /* File in cwd with no path separator — parent is cwd. */
        if (cap < 2) return 0;
        out[0] = '.';
        out[1] = '\0';
        return 1;
    }
    size_t n = (size_t)(slash - path);
    if (n == 0) {              /* root: "/foo" → "/" */
        if (cap < 2) return 0;
        out[0] = '/';
        out[1] = '\0';
        return 1;
    }
    if (n + 1 > cap) return 0;
    memcpy(out, path, n);
    out[n] = '\0';
    return 1;
}

static void cmd_yazi(const char *args) {
    /* Default starting dir: the dir of the current buffer's file when
     * that file exists on disk. Falls back to yazi's own default (cwd)
     * when there's no file, the file doesn't exist yet, or the user
     * supplied an explicit argument. */
    char auto_start[1024] = {0};
    if (!(args && *args)) {
        Buffer *cur = buf_cur();
        if (cur && cur->filename)
            dirname_if_exists(cur->filename, auto_start, sizeof(auto_start));
    }

    char tmppath[PATH_MAX];
    if (fs_temp_path("hed_yazi", tmppath, sizeof(tmppath)) != ED_OK) {
        ed_set_status_message("yazi: cannot create temp file");
        return;
    }

    const char *start_arg = (args && *args) ? args
                            : (auto_start[0] ? auto_start : NULL);

    StrBuf cmd = strbuf_new();
    strbuf_append(&cmd, "yazi --chooser-file=", 20);
    strbuf_append(&cmd, tmppath, strlen(tmppath));
    if (start_arg) {
        strbuf_append_char(&cmd, ' ');
        strbuf_append_shell_quoted(&cmd, start_arg);
    }
    char *cmd_str = strbuf_to_cstr(&cmd);
    strbuf_free(&cmd);
    if (!cmd_str) {
        ed_set_status_message("yazi: out of memory");
        fs_unlink(tmppath);
        return;
    }

    int status = term_cmd_run_interactive(cmd_str, false);
    free(cmd_str);
    if (status == -1) {
        ed_set_status_message("yazi: failed to launch (is it installed?)");
        fs_unlink(tmppath);
        ed_render_frame();
        return;
    }

    /* Read selected paths back. */
    FsLines *r = NULL;
    if (fs_lines_open(&r, tmppath) != ED_OK) {
        ed_set_status_message("yazi: no selection");
        fs_unlink(tmppath);
        ed_render_frame();
        return;
    }

    const char *line;
    size_t      len;
    int  opened = 0;
    char first_path[4096] = {0};
    while (fs_lines_next(r, &line, &len)) {
        if (!len) continue;
        if (!opened) snprintf(first_path, sizeof(first_path), "%s", line);
        buf_open_or_switch(line, true);
        opened++;
    }
    fs_lines_close(r);
    fs_unlink(tmppath);

    if (opened == 0) {
        ed_set_status_message("yazi: no selection");
    } else if (opened == 1) {
        ed_set_status_message("yazi: opened %s", first_path);
    } else {
        ed_set_status_message("yazi: opened %d files (last: %s)",
                              opened, first_path);
    }
    ed_render_frame();
}

static int yazi_init(void) {
    cmd("yazi", cmd_yazi, "pick a file with yazi and open it");
    return 0;
}

const Plugin plugin_yazi = {
    .name   = "yazi",
    .desc   = "yazi file picker integration",
    .init   = yazi_init,
    .deinit = NULL,
};
