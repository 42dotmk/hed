#include "hed.h"
#include "macros.h"
#include <sys/select.h>

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

    log_init(".hedlog");
    atexit(log_close);
    log_msg("=== HED START argc=%d ===", argc);
    log_msg("Before enable_raw_mode");
    enable_raw_mode();
    log_msg("Before ed_init, file_count=%d", file_count);
    ed_init(file_count == 0);
    log_msg("After ed_init, buffers.len=%zu, windows.len=%zu", E.buffers.len, E.windows.len);

    for (int i = 0; i < file_count; i++) {
        log_msg("Opening file %d: %s", i, file_args[i]);
        buf_open_or_switch(file_args[i], true);
        log_msg("After opening file %d, buffers.len=%zu", i, E.buffers.len);
    }

    free(file_args);

    /* If no buffers ended up being created (e.g., all opens failed), ensure one
     * exists. */
    log_msg("Checking buffer count: %zu", E.buffers.len);
    if (E.buffers.len == 0) {
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
    if (E.buffers.len > 0) {
        int last_idx = (int)E.buffers.len - 1;
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

    while (1) {
        ed_render_frame();

        /* If there are keys in the macro queue, process them immediately
         * without waiting for stdin */
        if (macro_queue_has_keys()) {
            ed_process_keypress();
            continue;
        }

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        int maxfd = STDIN_FILENO;

        int rc = select(maxfd + 1, &rfds, NULL, NULL, NULL);
        if (rc == -1) {
            if (errno == EINTR)
                continue;
            die("select");
        }

        if (FD_ISSET(STDIN_FILENO, &rfds)) {
            ed_process_keypress();
        }
    }

    return 0;
}
