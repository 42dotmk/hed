#include "hed.h"
/* UTF-8 aware: cx is a byte-index into row->chars. Returns visual columns. */
int buf_row_cx_to_rx(const Row *row, int cx) {
    int rx = 0;
    const char *s = row->chars.data;
    int len = (int)row->chars.len;
    if (cx > len)
        cx = len;
    for (int i = 0; i < cx;) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\t') {
            rx += (TAB_STOP - 1) - (rx % TAB_STOP);
            i++;
            rx++; /* tab char itself */
            continue;
        }
        int adv = 1;
        if ((c & 0x80) == 0)
            adv = 1; /* ASCII */
        else if ((c & 0xE0) == 0xC0)
            adv = 2; /* 2-byte */
        else if ((c & 0xF0) == 0xE0)
            adv = 3; /* 3-byte */
        else if ((c & 0xF8) == 0xF0)
            adv = 4; /* 4-byte */
        if (i + adv > cx)
            adv = cx - i; /* don't cross boundary */
        i += adv;
        rx += 1; /* assume width 1 for all non-tab chars */
    }
    return rx;
}

/* UTF-8 aware: returns byte-index (cx) for a desired visual column (rx). */
int buf_row_rx_to_cx(const Row *row, int rx) {
    int cur_rx = 0;
    int cx = 0;
    const char *s = row->chars.data;
    int len = (int)row->chars.len;
    while (cx < len) {
        unsigned char c = (unsigned char)s[cx];
        int adv = 1;
        int w = 1;
        if (c == '\t') {
            int add = (TAB_STOP - 1) - (cur_rx % TAB_STOP);
            w = 1 + add;
            adv = 1;
        } else if ((c & 0x80) == 0) {
            adv = 1;
        } else if ((c & 0xE0) == 0xC0) {
            adv = 2;
        } else if ((c & 0xF0) == 0xE0) {
            adv = 3;
        } else if ((c & 0xF8) == 0xF0) {
            adv = 4;
        }
        if (cur_rx + w > rx)
            return cx;
        cur_rx += w;
        cx += adv;
    }
    return cx;
}

void buf_row_update(Row *row) {
    int tabs = 0;
    for (size_t j = 0; j < row->chars.len; j++)
        if (row->chars.data[j] == '\t')
            tabs++;

    sstr_free(&row->render);
    row->render = sstr_new();
    sstr_reserve(&row->render, row->chars.len + tabs * (TAB_STOP - 1) + 1);

    for (size_t j = 0; j < row->chars.len; j++) {
        if (row->chars.data[j] == '\t') {
            sstr_append_char(&row->render, ' ');
            while (row->render.len % TAB_STOP != 0)
                sstr_append_char(&row->render, ' ');
        } else {
            sstr_append_char(&row->render, row->chars.data[j]);
        }
    }
}

void row_free(Row *row) {
    sstr_free(&row->chars);
    sstr_free(&row->render);
}
