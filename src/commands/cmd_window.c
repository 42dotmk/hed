#include "cmd_window.h"
#include "hed.h"

/*
 * WINDOW COMMAND IMPLEMENTATIONS
 * ===============================
 *
 * Simple wrappers around window management functions.
 */

void cmd_split(const char *args) {
    (void)args;
    windows_split_horizontal();
}

void cmd_vsplit(const char *args) {
    (void)args;
    windows_split_vertical();
}

void cmd_wfocus(const char *args) {
    (void)args;
    windows_focus_next();
}

void cmd_wclose(const char *args) {
    (void)args;
    windows_close_current();
}
