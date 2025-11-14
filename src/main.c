#include "hed.h"
#include <sys/select.h>

int main(int argc, char *argv[]) {
    log_init(".hedlog");
    atexit(log_close);
    log_msg("hed start argc=%d", argc);
    enable_raw_mode();
    ed_init();

    if (argc >= 2) {
        for (int i = 1; i < argc; i++) {
            Buffer *nb = NULL;
            EdError err = buf_open_file(argv[i], &nb);
            if ((err == ED_OK || err == ED_ERR_FILE_NOT_FOUND) && nb) {
                win_attach_buf(window_cur(), nb);
            }
        }
        E.current_buffer = 0;
        Window *win = window_cur();
        if (win) win->buffer_index = 0;
    }
    while (1) {
        ed_render_frame();

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        int maxfd = STDIN_FILENO;

        int tfd = term_pane_fd();
        if (tfd >= 0) {
            FD_SET(tfd, &rfds);
            if (tfd > maxfd) maxfd = tfd;
        }

        int rc = select(maxfd + 1, &rfds, NULL, NULL, NULL);
        if (rc == -1) {
            if (errno == EINTR) continue;
            die("select");
        }

        if (tfd >= 0 && FD_ISSET(tfd, &rfds)) {
            term_pane_poll();
        }
        if (FD_ISSET(STDIN_FILENO, &rfds)) {
            ed_process_keypress();
        }
    }

    return 0;
}
