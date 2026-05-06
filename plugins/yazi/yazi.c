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

#include <sys/stat.h>

static const char *yazi_tmpdir(void) {
    const char *t = getenv("TMPDIR");
    if (t && *t) return t;
    return "/tmp";
}

/* Append `s` to `dst` as a single-quoted shell literal. Returns the
 * number of bytes written (excluding NUL), or a value >= cap on
 * truncation (caller should treat as failure). */
static size_t shell_squote(char *dst, size_t cap, const char *s) {
    size_t off = 0;
    if (off + 1 >= cap) return cap;
    dst[off++] = '\'';
    for (; *s; s++) {
        if (*s == '\'') {
            /* close, literal escaped quote, reopen */
            if (off + 4 >= cap) return cap;
            dst[off++] = '\'';
            dst[off++] = '\\';
            dst[off++] = '\'';
            dst[off++] = '\'';
        } else {
            if (off + 1 >= cap) return cap;
            dst[off++] = *s;
        }
    }
    if (off + 1 >= cap) return cap;
    dst[off++] = '\'';
    dst[off] = '\0';
    return off;
}

/* Trim trailing CR/LF/spaces/tabs in place. */
static void rstrip(char *s) {
    size_t n = strlen(s);
    while (n > 0) {
        char c = s[n - 1];
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') s[--n] = '\0';
        else break;
    }
}

static void cmd_yazi(const char *args) {
    char tmppath[256];
    snprintf(tmppath, sizeof(tmppath), "%s/hed_yazi_%d",
             yazi_tmpdir(), (int)getpid());
    /* Pre-create the file so an empty-on-exit case is unambiguous and
     * we own it at 0600 even if yazi never writes to it. */
    int fd = open(tmppath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        ed_set_status_message("yazi: cannot create temp file: %s",
                              strerror(errno));
        return;
    }
    close(fd);

    char cmd[1024];
    int  n;
    if (args && *args) {
        char qarg[768];
        if (shell_squote(qarg, sizeof(qarg), args) >= sizeof(qarg)) {
            ed_set_status_message("yazi: argument too long");
            unlink(tmppath);
            return;
        }
        n = snprintf(cmd, sizeof(cmd),
                     "yazi --chooser-file=%s %s", tmppath, qarg);
    } else {
        n = snprintf(cmd, sizeof(cmd),
                     "yazi --chooser-file=%s", tmppath);
    }
    if (n < 0 || (size_t)n >= sizeof(cmd)) {
        ed_set_status_message("yazi: command too long");
        unlink(tmppath);
        return;
    }

    int status = term_cmd_run_interactive(cmd, false);
    if (status == -1) {
        ed_set_status_message("yazi: failed to launch (is it installed?)");
        unlink(tmppath);
        ed_render_frame();
        return;
    }

    /* Read selected paths back. */
    FILE *fp = fopen(tmppath, "r");
    if (!fp) {
        ed_set_status_message("yazi: no selection");
        unlink(tmppath);
        ed_render_frame();
        return;
    }

    char line[4096];
    int  opened = 0;
    char first_path[4096] = {0};
    while (fgets(line, sizeof(line), fp)) {
        rstrip(line);
        if (!line[0]) continue;
        if (!opened) snprintf(first_path, sizeof(first_path), "%s", line);
        buf_open_or_switch(line, true);
        opened++;
    }
    fclose(fp);
    unlink(tmppath);

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
