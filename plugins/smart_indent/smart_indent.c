/* smart_indent plugin: when the user inserts a newline, prefix the new
 * line with the same leading whitespace as the previous line. */

#include "hed.h"

static void hook_smart_indent(const HookCharEvent *event) {
    WIN(win)
    if (event->c != '\n') return;
    Buffer *buf = event->buf;
    if (!buf) return;
    if (win->cursor.y < 1) return;

    /* Copy the previous line's leading whitespace verbatim so a tab-
     * indented line yields a tab-indented continuation, not 4 spaces. */
    Row *prev_row = &buf->rows[win->cursor.y - 1];
    for (size_t i = 0; i < prev_row->chars.len; i++) {
        char c = prev_row->chars.data[i];
        if (c != ' ' && c != '\t') break;
        buf_insert_char_in(buf, c);
    }
}

static int smart_indent_init(void) {
    hook_register_char(HOOK_CHAR_INSERT, MODE_INSERT, "*", hook_smart_indent);
    return 0;
}

const Plugin plugin_smart_indent = {
    .name   = "smart_indent",
    .desc   = "carry previous line's indentation onto new lines",
    .init   = smart_indent_init,
    .deinit = NULL,
};
