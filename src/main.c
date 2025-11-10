#include "hed.h"

int main(int argc, char *argv[]) {
    log_init(".hedlog");
    atexit(log_close);
    log_msg("hed start argc=%d", argc);
    enable_raw_mode();
    ed_init();

    if (argc >= 2) {
        for (int i = 1; i < argc; i++) {
            buf_open(argv[i]);
        }
        E.current_buffer = 0;
        Window *win = window_cur();
        if (win) win->buffer_index = 0;
    }
    ed_set_status_message("");
    while (1) {
        buf_refresh_screen();
        ed_process_keypress();
    }

    return 0;
}
