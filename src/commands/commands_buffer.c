/*
 * BUFFER AND FILE COMMAND MODULE
 * ===============================
 *
 * Consolidated buffer management and file I/O commands.
 * Combines cmd_buffer.c and cmd_file.c.
 */

#include "commands/commands_buffer.h"
#include "editor.h"
#include "fs/fs.h"
#include "hooks.h"
#include "lib/strutil.h"
#include "picker.h"
#include "terminal.h"
#include "commands/cmd_util.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ============================================
 * BUFFER NAVIGATION AND MANAGEMENT COMMANDS
 * ============================================ */

void cmd_buffer_next(const char *args) {
    (void)args;
    buf_next();
}

void cmd_buffer_prev(const char *args) {
    (void)args;
    buf_prev();
}

void cmd_buffer_alt(const char *args) {
    (void)args;
    Buffer *cur = buf_cur();
    const char *cur_fn = cur ? cur->filename : NULL;
    for (ptrdiff_t i = arrlen(E.jump_list.entries) - 1; i >= 0; i--) {
        const char *fp = E.jump_list.entries[i].filepath;
        if (!fp) continue;
        if (cur_fn && strcmp(fp, cur_fn) == 0) continue;
        int idx = buf_find_by_filename(fp);
        if (idx >= 0 && idx != E.current_buffer) {
            buf_switch(idx);
            return;
        }
    }
    ed_set_status_message("No alternate buffer");
}

void cmd_buffer_list(const char *args) {
    if (arrlen(E.buffers) == 0) {
        ed_set_status_message("No buffers");
        return;
    }
    if (picker_invoke("buffers", args)) return;
    ed_set_status_message("buffers: %td open (no picker installed)",
                          arrlen(E.buffers));
}

void cmd_buffer_switch(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("Usage: :b <buffer_number>");
        return;
    }
    int buf_idx = atoi(args) - 1;
    EdError err = buf_switch(buf_idx);
    if (err != ED_OK) {
        ed_set_status_message("Failed to switch: %s", ed_error_string(err));
    } else {
        Buffer *buf = buf_cur();
        ed_set_status_message("Switched to buffer %d: %s", buf_idx + 1,
                              buf->title);
    }
}

void cmd_buffer_delete(const char *args) {
    int buf_idx;
    if (!args || !*args) {
        buf_idx = E.current_buffer;
    } else {
        buf_idx = atoi(args) - 1;
    }

    EdError err = buf_close(buf_idx);
    if (err != ED_OK) {
        switch (err) {
        case ED_ERR_INVALID_INDEX:
            ed_set_status_message("Invalid buffer index");
            break;
        case ED_ERR_BUFFER_DIRTY:
            ed_set_status_message(
                "Buffer has unsaved changes! Save first or use :bd!");
            break;
        default:
            ed_set_status_message("Error closing buffer: %s",
                                  ed_error_string(err));
            break;
        }
    } else {
        ed_set_status_message("Buffer closed");
    }
}

void cmd_buffer_delete_force(const char *args) {
    int buf_idx;
    if (!args || !*args) {
        buf_idx = E.current_buffer;
    } else {
        buf_idx = atoi(args) - 1;
    }

    if (buf_idx < 0 || buf_idx >= (int)arrlen(E.buffers)) {
        ed_set_status_message("Invalid buffer index");
        return;
    }

    E.buffers[buf_idx].dirty = 0;

    EdError err = buf_close(buf_idx);
    if (err != ED_OK) {
        ed_set_status_message("Error closing buffer: %s",
                              ed_error_string(err));
    } else {
        ed_set_status_message("Buffer closed (forced)");
    }
}



void cmd_buffers(const char *args) {
    if (arrlen(E.buffers) <= 0) {
        ed_set_status_message("no buffers");
        return;
    }
    if (picker_invoke("buffers", args)) return;
    ed_set_status_message("buffers: %td open (no picker installed)",
                          arrlen(E.buffers));
}

/* ============================================
 * FILE I/O AND LIFECYCLE COMMANDS
 * ============================================ */

void cmd_quit(const char *args) {
    (void)args;
    Buffer *buf = buf_cur();
    if (buf && buf->dirty) {
        ed_set_status_message(
            "File has unsaved changes! Use :q! to force quit");
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
    if (!buf)
        return;

    /* Allow plugins (e.g., dired) to intercept the save. */
    {
        HookBufferEvent ev = {0};
        ev.buf = buf;
        ev.filename = buf->filename;
        hook_fire_buffer(HOOK_BUFFER_SAVE_PRE, &ev);
        if (ev.consumed)
            return;
    }

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
            buf->filetype = fs_path_detect_filetype(exppath);
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
    buf_open_or_switch(exppath, true);
}

void cmd_cd(const char *args) {
    if (!args || !*args) {
        if (E.cwd[0]) {
            ed_set_status_message("cwd: %s", E.cwd);
        } else {
            char cwd[PATH_MAX];
            if (fs_getcwd(cwd, sizeof(cwd))) {
                ed_set_status_message("cwd: %s", cwd);
            } else {
                ed_set_status_message("cwd: (unknown)");
            }
        }
        return;
    }

    char trimmed[PATH_MAX];
    char path[PATH_MAX];
    str_trim_whitespace(args, trimmed, sizeof(trimmed));
    fs_path_expand_tilde(trimmed, path, sizeof(path));

    EdError err = fs_chdir(path);
    if (err == ED_OK) {
        if (fs_getcwd(E.cwd, sizeof(E.cwd))) {
            ed_set_status_message("cd: %s", E.cwd);
        } else {
            E.cwd[0] = '\0';
            ed_set_status_message("cd: ok");
        }
    } else {
        ed_set_status_message("cd: %s", ed_error_string(err));
    }
}
