/* auto_pair plugin: when the user types an opening bracket/quote,
 * insert the matching closing one and step the cursor back so the
 * user can keep typing inside the pair. */

#include "hed.h"

static void hook_auto_pair(const HookCharEvent *event) {
    BUFWIN(buf, win);
    char inserted = 0;
    switch (event->c) {
    case '(':  inserted = ')'; break;
    case '[':  inserted = ']'; break;
    case '<':  inserted = '>'; break;
    case '{':  inserted = '}'; break;
    case '"':  inserted = '"'; break;
    case '\'': inserted = '\''; break;
    case '`':  inserted = '`'; break;
    default:   break;
    }
    if (inserted) {
        buf_insert_char_in(buf, inserted);
        win->cursor.x--;
    }
}

static int auto_pair_init(void) {
    hook_register_char(HOOK_CHAR_INSERT, MODE_INSERT, "*", hook_auto_pair);
    return 0;
}

const Plugin plugin_auto_pair = {
    .name   = "auto_pair",
    .desc   = "auto-insert matching brackets and quotes",
    .init   = auto_pair_init,
    .deinit = NULL,
};
