/* smart_indent plugin: when the user inserts a newline, prefix the new
 * line with the same leading whitespace as the previous line. */

#include "../plugin.h"
#include "hed.h"

static void hook_smart_indent(const HookCharEvent *event) {
    WIN(win)
    if (event->c != '\n') return;
    Buffer *buf = event->buf;
    if (!buf) return;
    if (win->cursor.y < 1) return;

    Row *prev_row = &buf->rows[win->cursor.y - 1];
    int prev_indent = 0;
    for (size_t i = 0; i < prev_row->chars.len; i++) {
        if (prev_row->chars.data[i] == ' ')
            prev_indent++;
        else if (prev_row->chars.data[i] == '\t')
            prev_indent += 4; /* assuming tab width of 4 */
        else
            break;
    }
    for (int i = 0; i < prev_indent; i++) {
        buf_insert_char_in(buf, ' ');
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
