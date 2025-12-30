#include "hook_builtins.h"
#include "hed.h"

void hook_change_cursor_shape(const HookModeEvent *event) {
    ed_change_cursor_shape();
    (void)event;
}


void hook_auto_pair(const HookCharEvent *event) {
    BUFWIN(buf, win);
	char inserted = 0;
    switch (event->c) {
    case '(':
        inserted = ')';
        break;
    case '[':
        inserted = ']';
        break;
    case '<':
        inserted = '>';
        break;
    case '{':
        inserted = '}';
        break;

    case '"':
         inserted = '"';
         break;
    case '\'':
         inserted = '\'';
		 break;
    case '`':
        inserted = '`';
        break;
    default:
        break;
    }
    if (inserted) {
        buf_insert_char_in(buf, inserted);
        buf->cursor.x--;
    }
}

void hook_smart_indent(const HookCharEvent *event) {
    WIN(win)
    if (event->c == '\n') {
        Buffer *buf = event->buf;
        if (!buf) return;
        Row *prev_row = &buf->rows[buf->cursor.y - 1];
        int prev_indent = 0;
        for (size_t i = 0; i < prev_row->chars.len; i++) {
            if (prev_row->chars.data[i] == ' ')
                prev_indent++;
            else if (prev_row->chars.data[i] == '\t')
                prev_indent += 4; // assuming tab width of 4
            else
                break;
        }
        for (int i = 0; i < prev_indent; i++) {
            buf_insert_char_in(buf, ' ');
        }
    }
}
