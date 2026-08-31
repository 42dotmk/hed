/* auto_pair plugin: when the user types an opening bracket/quote,
 * insert the matching closing one and step the cursor back so the
 * user can keep typing inside the pair. Typing a closer that is
 * already the next char skips over it instead of doubling it, and
 * backspacing an opener whose closer sits to the right deletes both
 * halves. */

#include "hed.h"

static char closer_of(int c) {
    switch (c) {
    case '(':
        return ')';
    case '[':
        return ']';
    case '<':
        return '>';
    case '{':
        return '}';
    case '"':
    case '\'':
    case '`':
        return (char)c;
    default:
        return 0;
    }
}

static int is_closer(int c) {
    return c == ')' || c == ']' || c == '>' || c == '}' || c == '"' ||
           c == '\'' || c == '`';
}

/* Guard: our own buf_del_char_in calls re-fire HOOK_CHAR_DELETE, and
 * hook_pair_delete must not chain off them (a skipped-over quote would
 * otherwise eat its partner). */
static int own_delete;

static void hook_auto_pair(const HookCharEvent *event) {
    BUFWIN(buf, win);
    if (is_closer(event->c) && win->cursor.y < buf->num_rows) {
        Row *row = &buf->rows[win->cursor.y];
        if ((size_t)win->cursor.x < row->chars.len &&
            row->chars.data[win->cursor.x] == event->c) {
            /* Drop the just-typed duplicate, step over the existing
             * closer. */
            own_delete = 1;
            buf_del_char_in(buf);
            own_delete = 0;
            win->cursor.x++;
            return;
        }
    }
    char inserted = closer_of(event->c);
    if (inserted) {
        buf_insert_char_in(buf, inserted);
        win->cursor.x--;
    }
}

/* Backspace on an opener whose closer is right of the cursor removes
 * the closer too, so an unwanted pair dies with one keypress. This
 * fires mid-delete (before the caller pulls the cursor back), so no
 * BUFWIN asserts here: event->row/col name the deleted opener's slot,
 * where the closer now sits. */
static void hook_pair_delete(const HookCharEvent *event) {
    if (own_delete)
        return;
    Buffer *buf = event->buf;
    Window *win = window_cur();
    char close = closer_of(event->c);
    if (!close || !buf || !win)
        return;
    int y = event->row, x = event->col;
    if (y < 0 || y >= buf->num_rows || x < 0)
        return;
    Row *row = &buf->rows[y];
    if ((size_t)x < row->chars.len && row->chars.data[x] == close) {
        win->cursor.y = y;
        win->cursor.x = x + 1;
        own_delete = 1;
        buf_del_char_in(buf);
        own_delete = 0;
    }
}

static int auto_pair_init(void) {
    hook_register_char(HOOK_CHAR_INSERT, MODE_INSERT, "*", hook_auto_pair);
    hook_register_char(HOOK_CHAR_DELETE, MODE_INSERT, "*", hook_pair_delete);
    return 0;
}

const Plugin plugin_auto_pair = {
    .name = "auto_pair",
    .desc = "auto-insert matching brackets and quotes",
    .init = auto_pair_init,
    .deinit = NULL,
};
