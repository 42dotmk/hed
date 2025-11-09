#include "hed.h"

int main(int argc, char *argv[]) {
    enable_raw_mode();
    ed_init();

    if (argc >= 2) {
        for (int i = 1; i < argc; i++) {
            buf_open(argv[i]);
        }
        E.current_buffer = 0;
    }
    ed_set_status_message("");
    while (1) {
        buf_refresh_screen();
        ed_process_keypress();
    }

    return 0;
}
