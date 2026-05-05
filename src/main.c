#include "editor.h"
#include "lib/log.h"
#include "hooks.h"
#include "commands.h"
#include "terminal.h"
#include "select_loop.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "macros.h"
#include "lib/file_helpers.h"

static void on_stdin_readable(int fd, void *ud) {
    (void)fd; (void)ud;
    ed_process_keypress();
}


int main(int argc, char *argv[]) {
    const char *startup_cmd = NULL;
    char **file_args = NULL;
    int file_count = 0;

    if (argc > 1) {
        file_args = calloc((size_t)(argc - 1), sizeof(char *));
        if (!file_args) {
            fprintf(stderr, "hed: out of memory\n");
            return 1;
        }
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "hed: -c requires an argument\n");
                free(file_args);
                return 1;
            }
            startup_cmd = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--") == 0) {
            for (int j = i + 1; j < argc; j++) {
                file_args[file_count++] = argv[j];
            }
            break;
        }
        file_args[file_count++] = argv[i];
    }

    char log_path[4096];
    if (path_cache_file_for_cwd("log", log_path, sizeof(log_path))) {
        log_init(log_path);
    } else {
        log_init(".hedlog");
    }
    atexit(log_close);
    log_msg("=== HED START argc=%d ===", argc);
    log_msg("Before enable_raw_mode");
    enable_raw_mode();
    log_msg("Before ed_init, file_count=%d", file_count);
    ed_init(file_count == 0);
    log_msg("After ed_init, buffers.len=%zu, windows.len=%zu", arrlen(E.buffers), arrlen(E.windows));

    for (int i = 0; i < file_count; i++) {
        log_msg("Opening file %d: %s", i, file_args[i]);
        buf_open_or_switch(file_args[i], true);
        log_msg("After opening file %d, buffers.len=%zu", i, arrlen(E.buffers));
    }

    free(file_args);

    /* If no buffers ended up being created (e.g., all opens failed), ensure one
     * exists. */
    log_msg("Checking buffer count: %zu", arrlen(E.buffers));
    if (arrlen(E.buffers) == 0) {
        log_msg("Creating empty buffer");
        int empty_idx = -1;
        if (buf_new(NULL, &empty_idx) == ED_OK) {
            E.current_buffer = empty_idx;
            Window *win = window_cur();
            if (win)
                win->buffer_index = empty_idx;
        }
        log_msg("After empty buffer creation");
    }

    /* Focus the most recently opened buffer */
    if (arrlen(E.buffers) > 0) {
        int last_idx = (int)arrlen(E.buffers) - 1;
        E.current_buffer = last_idx;
        Window *win = window_cur();
        if (win)
            win->buffer_index = last_idx;
    }

    /* Run startup command passed via -c "<command>" */
    if (startup_cmd && *startup_cmd) {
        const char *cmdline = startup_cmd;
        if (*cmdline == ':')
            cmdline++;
        while (*cmdline && isspace((unsigned char)*cmdline))
            cmdline++;
        char name[128];
        size_t nlen = 0;
        while (cmdline[nlen] && !isspace((unsigned char)cmdline[nlen]) &&
               nlen < sizeof(name) - 1) {
            name[nlen] = cmdline[nlen];
            nlen++;
        }
        name[nlen] = '\0';
        const char *args = NULL;
        if (cmdline[nlen]) {
            args = cmdline + nlen + 1;
            while (args && *args && isspace((unsigned char)*args))
                args++;
            if (args && *args == '\0')
                args = NULL;
        }
        if (name[0]) {
            if (!command_execute(name, args)) {
                ed_set_status_message("Unknown command: %s", name);
            }
        }
    }

    ed_loop_register("stdin", STDIN_FILENO, on_stdin_readable, NULL);

    hook_fire_simple(HOOK_STARTUP_DONE);

    while (1) {
        ed_render_frame();

        if (macro_queue_has_keys()) {
            ed_process_keypress();
            continue;
        }

        if (ed_loop_select_once() < 0)
            die("select");
    }

    return 0;
}
