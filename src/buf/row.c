#include "editor.h"
#include "buf/row.h"
#include "lib/errors.h"
#include "lib/log.h"
#include "lib/strutil.h"
#include <stdlib.h>
#include <string.h>

/* cx is a byte-index into row->chars. Returns the visual column (display
 * width) of that position, expanding tabs to TAB_STOP and using wcwidth() for
 * every other codepoint so wide (CJK/emoji) chars count as 2 and combining
 * marks as 0. This is the same metric utf8_display_width() uses, so the
 * renderer and the cursor agree on where every glyph sits. */
int buf_row_cx_to_rx(const Row *row, int cx) {
    int rx = 0;
    const char *s = row->chars.data;
    int len = (int)row->chars.len;
    if (cx > len)
        cx = len;
    for (int i = 0; i < cx;) {
        if (s[i] == '\t') {
            rx += TAB_STOP - (rx % TAB_STOP);
            i++;
            continue;
        }
        int adv = 1;
        int w = utf8_char_width(s + i, (size_t)(len - i), &adv);
        if (adv < 1)
            adv = 1;
        if (i + adv > cx)
            break; /* cx lands inside this codepoint; treat as its start */
        i += adv;
        rx += w;
    }
    return rx;
}

/* Returns the byte-index (cx) of the codepoint occupying visual column `rx`,
 * the inverse of buf_row_cx_to_rx. A wide char straddling the target column
 * resolves to its own start byte. */
int buf_row_rx_to_cx(const Row *row, int rx) {
    int cur_rx = 0;
    int cx = 0;
    const char *s = row->chars.data;
    int len = (int)row->chars.len;
    while (cx < len) {
        int adv = 1;
        int w;
        if (s[cx] == '\t') {
            w = TAB_STOP - (cur_rx % TAB_STOP);
        } else {
            w = utf8_char_width(s + cx, (size_t)(len - cx), &adv);
            if (adv < 1)
                adv = 1;
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
