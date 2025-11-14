#include "cmd_file.h"
#include "../hed.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

void cmd_quit(const char *args) {
    (void)args;
    Buffer *buf = buf_cur();
    if (buf && buf->dirty) {
        ed_set_status_message("File has unsaved changes! Use :q! to force quit");
    } else {
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
    }
}

void cmd_quit_force(const char *args) {
    (void)args;
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
}

void cmd_write(const char *args) {
    Buffer *buf = buf_cur();
    if (!buf) return;

    /* If a filename is provided, set it on the buffer before saving */
    if (args && *args) {
        char newname[PATH_MAX];
        size_t n = str_trim_whitespace(args, newname, sizeof(newname));
        if (n > 0 && newname[0]) {
            char exppath[PATH_MAX];
            str_expand_tilde(newname, exppath, sizeof(exppath));
            free(buf->filename);
            buf->filename = strdup(exppath);
            free(buf->title);
            buf->title = strdup(exppath);
            /* update filetype */
            free(buf->filetype);
            buf->filetype = buf_detect_filetype(exppath);
        }
    }
    EdError err = buf_save_in(buf);
    if (err != ED_OK) {
        ed_set_status_message("Save failed: %s", ed_error_string(err));
    }
}

void cmd_write_quit(const char *args) {
    (void)args;
    EdError err = buf_save_in(buf_cur());
    if (err != ED_OK) {
        ed_set_status_message("Save failed: %s", ed_error_string(err));
        return;
    }
    Buffer *buf = buf_cur();
    if (buf && !buf->dirty) {
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
    }
}

void cmd_edit(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("Usage: :e <filename>");
        return;
    }

    char trimmed[PATH_MAX];
    char exppath[PATH_MAX];
    str_trim_whitespace(args, trimmed, sizeof(trimmed));
    str_expand_tilde(trimmed, exppath, sizeof(exppath));

    /* Prefer opening a buffer object then attaching to the window */
    Buffer *nb = NULL;
    EdError err = buf_open_file(exppath, &nb);
    if (err == ED_OK || err == ED_ERR_FILE_NOT_FOUND) {
        /* Success or new file (both OK) */
        if (nb) {
            win_attach_buf(window_cur(), nb);
        }
    } else {
        ed_set_status_message("Failed to open: %s", ed_error_string(err));
    }
}

void cmd_cd(const char *args) {
    char cwd[PATH_MAX];
    if (!args || !*args) {
        if (getcwd(cwd, sizeof(cwd))) {
            ed_set_status_message("cwd: %s", cwd);
        } else {
            ed_set_status_message("cwd: (unknown)");
        }
        return;
    }

    char trimmed[PATH_MAX];
    char path[PATH_MAX];
    str_trim_whitespace(args, trimmed, sizeof(trimmed));
    str_expand_tilde(trimmed, path, sizeof(path));

    if (chdir(path) == 0) {
        if (getcwd(cwd, sizeof(cwd))) {
            ed_set_status_message("cd: %s", cwd);
        } else {
            ed_set_status_message("cd: ok");
        }
    } else {
        ed_set_status_message("cd: %s", strerror(errno));
    }
}
