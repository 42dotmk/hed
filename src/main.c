#include "editor.h"
#include "lib/log.h"
#include "hooks.h"
#include "commands.h"
#include "terminal.h"
#include "select_loop.h"
#include "macros.h"
#include "lib/file_helpers.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    const char  *startup_cmd;  /* -c argument; not owned */
    char       **files;        /* heap array of argv pointers */
    int          file_count;
} CliArgs;

/* ------------------------------------------------------------------------- */
/* Argument parsing                                                          */

/* Parse hed's command line. Recognised forms:
 *   hed [files...]
 *   hed -c "<command>" [files...]
 *   hed [files...] -- [more files starting with '-']
 * Returns 0 on success, non-zero on usage error (and writes to stderr). */
static int parse_args(int argc, char *argv[], CliArgs *out) {
    out->startup_cmd = NULL;
    out->files       = NULL;
    out->file_count  = 0;

    if (argc <= 1) return 0;

    out->files = calloc((size_t)(argc - 1), sizeof(char *));
    if (!out->files) {
        fprintf(stderr, "hed: out of memory\n");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "hed: -c requires an argument\n");
                free(out->files);
                out->files = NULL;
                return 1;
            }
            out->startup_cmd = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--") == 0) {
            for (int j = i + 1; j < argc; j++)
                out->files[out->file_count++] = argv[j];
            break;
        }
        out->files[out->file_count++] = argv[i];
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Logging                                                                   */

static void init_logging(int argc) {
    char log_path[4096];
    if (path_cache_file_for_cwd("log", log_path, sizeof(log_path)))
        log_init(log_path);
    else
        log_init(".hedlog");
    atexit(log_close);
    log_msg("=== HED START argc=%d ===", argc);
}

/* ------------------------------------------------------------------------- */
/* Buffer setup                                                              */

/* Open the files supplied on the command line, ensure at least one
 * buffer exists, and focus the most recently opened one in the
 * current window. */
static void open_initial_buffers(char **files, int file_count) {
    for (int i = 0; i < file_count; i++)
        buf_open_or_switch(files[i], true);

    if (arrlen(E.buffers) == 0) {
        int empty_idx = -1;
        if (buf_new(NULL, &empty_idx) == ED_OK) {
            E.current_buffer = empty_idx;
            Window *win = window_cur();
            if (win) win->buffer_index = empty_idx;
        }
    }

    if (arrlen(E.buffers) > 0) {
        int last_idx = (int)arrlen(E.buffers) - 1;
        E.current_buffer = last_idx;
        Window *win = window_cur();
        if (win) win->buffer_index = last_idx;
    }
}

/* ------------------------------------------------------------------------- */
/* Startup command (-c "<command>")                                          */

/* Skip the optional leading ':' and any whitespace. */
static const char *strip_command_prefix(const char *s) {
    if (*s == ':') s++;
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

/* Split "name [args...]" into a name buffer and an args pointer that
 * points into the original string (or NULL when no args remain). */
static void split_command_line(const char *cmdline,
                                char *name, size_t name_cap,
                                const char **args_out) {
    size_t nlen = 0;
    while (cmdline[nlen] && !isspace((unsigned char)cmdline[nlen]) &&
           nlen < name_cap - 1) {
        name[nlen] = cmdline[nlen];
        nlen++;
    }
    name[nlen] = '\0';

    const char *args = NULL;
    if (cmdline[nlen]) {
        args = cmdline + nlen + 1;
        while (*args && isspace((unsigned char)*args)) args++;
        if (*args == '\0') args = NULL;
    }
    *args_out = args;
}

static void run_startup_command(const char *cmdline) {
    if (!cmdline || !*cmdline) return;

    cmdline = strip_command_prefix(cmdline);
    char        name[128];
    const char *args = NULL;
    split_command_line(cmdline, name, sizeof(name), &args);

    if (!name[0]) return;
    if (!command_execute(name, args))
        ed_set_status_message("Unknown command: %s", name);
}

/* ------------------------------------------------------------------------- */
/* Event loop                                                                */

static void on_stdin_readable(int fd, void *ud) {
    (void)fd; (void)ud;
    ed_process_keypress();
}

static void event_loop(void) {
    while (1) {
        ed_render_frame();

        /* Drain queued macro keystrokes without going through select(),
         * since they have no fd to wake us. */
        if (macro_queue_has_keys()) {
            ed_process_keypress();
            continue;
        }

        if (ed_loop_select_once() < 0)
            die("select");
    }
}

/* ------------------------------------------------------------------------- */
/* Entry point                                                               */

int main(int argc, char *argv[]) {
    CliArgs args;
    if (parse_args(argc, argv, &args) != 0)
        return 1;

    init_logging(argc);
    enable_raw_mode();
    ed_init(args.file_count == 0);

    open_initial_buffers(args.files, args.file_count);
    free(args.files);

    run_startup_command(args.startup_cmd);

    ed_loop_register("stdin", STDIN_FILENO, on_stdin_readable, NULL);
    hook_fire_simple(HOOK_STARTUP_DONE);

    event_loop();
    return 0;
}
