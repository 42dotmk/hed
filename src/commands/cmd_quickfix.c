#include "cmd_quickfix.h"
#include "cmd_util.h"
#include "../hed.h"
#include <string.h>
#include <stdlib.h>

void cmd_copen(const char *args) {
    int h = parse_int_default(args, 8);
    if (h < 2) h = 2;
    qf_open(&E.qf, h);
}

void cmd_cclose(const char *args) {
    (void)args;
    qf_close(&E.qf);
}

void cmd_ctoggle(const char *args) {
    int h = parse_int_default(args, E.qf.height > 0 ? E.qf.height : 8);
    qf_toggle(&E.qf, h);
}

void cmd_cclear(const char *args) {
    (void)args;
    qf_clear(&E.qf);
}

static void cadd_current(const char *msg) {
    Buffer *b = buf_cur();
    Window *win = window_cur();
    const char *fn = b && b->filename ? b->filename : NULL;
    int line = (b && win) ? win->cursor_y + 1 : 1;
    int col  = (b && win) ? win->cursor_x + 1 : 1;
    qf_add(&E.qf, fn, line, col, msg ? msg : "");
}

void cmd_cadd(const char *args) {
    if (!args || !*args) {
        cadd_current("");
        return;
    }

    /* Try to parse form: file:line[:col]: message */
    const char *p1 = strchr(args, ':');
    if (!p1) {
        cadd_current(args);
        return;
    }

    const char *p2 = strchr(p1 + 1, ':');
    const char *p3 = p2 ? strchr(p2 + 1, ':') : NULL;
    char file[256] = {0};
    int line = 0, col = 0;
    const char *msg = NULL;

    if (p3) {
        /* file:line:col: msg */
        size_t flen = (size_t)(p1 - args);
        if (flen >= sizeof(file)) flen = sizeof(file) - 1;
        memcpy(file, args, flen);
        file[flen] = '\0';
        line = atoi(p1 + 1);
        col = atoi(p2 + 1);
        msg = p3 + 1;
        qf_add(&E.qf, file, line, col, msg);
    } else if (p2) {
        /* file:line: msg */
        size_t flen = (size_t)(p1 - args);
        if (flen >= sizeof(file)) flen = sizeof(file) - 1;
        memcpy(file, args, flen);
        file[flen] = '\0';
        line = atoi(p1 + 1);
        msg = p2 + 1;
        qf_add(&E.qf, file, line, 1, msg);
    } else {
        cadd_current(args);
    }
}

void cmd_cnext(const char *args) {
    (void)args;
    if (E.qf.len == 0) {
        ed_set_status_message("Quickfix empty");
        return;
    }
    qf_move(&E.qf, 1);
    qf_open_selected(&E.qf);
}

void cmd_cprev(const char *args) {
    (void)args;
    if (E.qf.len == 0) {
        ed_set_status_message("Quickfix empty");
        return;
    }
    qf_move(&E.qf, -1);
    qf_open_selected(&E.qf);
}

void cmd_copenidx(const char *args) {
    int idx = parse_int_default(args, 1);
    if (idx <= 0) idx = 1;
    if (idx > E.qf.len) idx = E.qf.len;
    qf_open_idx(&E.qf, idx - 1);
}
