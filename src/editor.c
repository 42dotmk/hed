#include "hed.h"

Ed E;

void ed_change_cursor_shape(void) {
    switch (E.mode) {
        case MODE_NORMAL:
            write(STDOUT_FILENO, CURSOR_STYLE_BLOCK, 5);
            break;
        case MODE_INSERT:
            write(STDOUT_FILENO,CURSOR_STYLE_BEAM, 5);
            break;
        case MODE_VISUAL:
            write(STDOUT_FILENO,CURSOR_STYLE_UNDERLINE, 5);
            break;
        case MODE_COMMAND:
            write(STDOUT_FILENO,CURSOR_STYLE_BLOCK, 5);
            break;
    }
}

void ed_set_mode(EditorMode new_mode) {
    if (E.mode == new_mode) return;

    EditorMode old_mode = E.mode;
    E.mode = new_mode;

    /* Clear keybind buffer when changing modes */
    keybind_clear_buffer();

    /* Fire mode change hook */
    HookModeEvent event = {old_mode, new_mode};
    hook_fire_mode(HOOK_MODE_CHANGE, &event);
}
int ed_read_key(void) {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    if (c == '\x1b') {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '3': return 127; /* Delete */
                        case '5': return 1000; /* Page Up */
                        case '6': return 1001; /* Page Down */
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return 1002; /* Up */
                    case 'B': return 1003; /* Down */
                    case 'C': return 1004; /* Right */
                    case 'D': return 1005; /* Left */
                    case 'H': return 1006; /* Home */
                    case 'F': return 1007; /* End */
                }
            }
        }

        return '\x1b';
    } else {
        return c;
    }
}

void ed_move_cursor(int key) {
    Buffer *buf = buf_cur();
    if (!buf) return;

    Row *row = (buf->cursor_y >= buf->num_rows) ? NULL : &buf->rows[buf->cursor_y];

    switch (key) {
        case 1005: /* Left */
        case 'h':
            if (buf->cursor_x != 0) {
                buf->cursor_x--;
            } else if (buf->cursor_y > 0) {
                buf->cursor_y--;
                buf->cursor_x = buf->rows[buf->cursor_y].chars.len;
            }
            break;
        case 1003: /* Down */
        case 'j':
            if (buf->cursor_y < buf->num_rows - 1) {
                buf->cursor_y++;
            }
            break;
        case 1002: /* Up */
        case 'k':
            if (buf->cursor_y != 0) {
                buf->cursor_y--;
            }
            break;
        case 1004: /* Right */
        case 'l':
            if (row && buf->cursor_x < (int)row->chars.len) {
                buf->cursor_x++;
            } else if (row && buf->cursor_x == (int)row->chars.len && buf->cursor_y < buf->num_rows - 1) {
                buf->cursor_y++;
                buf->cursor_x = 0;
            }
            break;
    }

    row = (buf->cursor_y >= buf->num_rows) ? NULL : &buf->rows[buf->cursor_y];
    int rowlen = row ? row->chars.len : 0;
    if (buf->cursor_x > rowlen) {
        buf->cursor_x = rowlen;
    }
}

void ed_process_command(void) {
    if (E.command_len == 0) {
        ed_set_mode(MODE_NORMAL);
        return;
    }

    E.command_buf[E.command_len] = '\0';

    /* Parse command name and arguments */
    char *space = strchr(E.command_buf, ' ');
    char *cmd_name = E.command_buf;
    char *cmd_args = NULL;

    if (space) {
        *space = '\0';  /* Split command name and args */
        cmd_args = space + 1;
    }

    /* Execute command */
    if (!command_execute(cmd_name, cmd_args)) {
        /* Restore space if we modified the buffer */
        if (space) *space = ' ';
        ed_set_status_message("Unknown command: %s", E.command_buf);
    }

    ed_set_mode(MODE_NORMAL);
    E.command_len = 0;
}

void ed_process_keypress(void) {
    int c = ed_read_key();
    Buffer *buf = buf_cur();

    if (E.mode == MODE_COMMAND) {
        if (c == '\r') {
            ed_process_command();
        } else if (c == '\x1b') {
            E.mode = MODE_NORMAL;
            E.command_len = 0;
        } else if (c == 127 || c == CTRL_KEY('h')) {
            if (E.command_len > 0) E.command_len--;
        } else if (!iscntrl(c) && c < 128) {
            if (E.command_len < (int)sizeof(E.command_buf) - 1) {
                E.command_buf[E.command_len++] = c;
            }
        }
        return;
    }

    if (E.mode == MODE_INSERT) {
        /* Try keybindings first (e.g., Ctrl+S to save in insert mode) */
        if (keybind_process(c, E.mode)) {
            return;  /* Keybind handled the key */
        }

        switch (c) {
            case '\x1b':
                ed_set_mode(MODE_NORMAL);
                if (buf && buf->cursor_x > 0) buf->cursor_x--;
                break;
            case '\r':
                buf_insert_newline();
                break;
            case 127:
            case CTRL_KEY('h'):
                buf_del_char();
                break;
            case 1002: case 1003: case 1004: case 1005:
                ed_move_cursor(c);
                break;
            default:
                if (!iscntrl(c)) {
                    buf_insert_char(c);
                }
                break;
        }
        return;
    }

    if (E.mode == MODE_VISUAL) {
        /* Try keybindings first */
        if (keybind_process(c, E.mode)) {
            return;  /* Keybind handled the key */
        }

        switch (c) {
            case '\x1b':
                ed_set_mode(MODE_NORMAL);
                break;
            case 'h': case 'j': case 'k': case 'l':
            case 1002: case 1003: case 1004: case 1005:
                ed_move_cursor(c);
                break;
        }
        return;
    }

    /* Normal mode */
    if (!buf) return;

    /* Try keybindings first */
    if (keybind_process(c, E.mode)) {
        return;  /* Keybind handled the key */
    }

    /* Fallback handlers for keys not bound */
    switch (c) {
        case 'h': case 'j': case 'k': case 'l':
        case 1002: case 1003: case 1004: case 1005:
            ed_move_cursor(c);
            break;
        case '/':
            /* Simple search prompt */
            ed_set_mode(MODE_COMMAND);
            E.command_len = 0;
            ed_set_status_message("Search: ");
            buf_refresh_screen();

            /* Read search query */
            int search_len = 0;
            char search_buf[80];
            while (1) {
                int k = ed_read_key();
                if (k == '\r') break;
                if (k == '\x1b') {
                    ed_set_mode(MODE_NORMAL);
                    return;
                }
                if (k == 127 && search_len > 0) {
                    search_len--;
                } else if (!iscntrl(k) && k < 128 && search_len < 79) {
                    search_buf[search_len++] = k;
                }
                search_buf[search_len] = '\0';
                ed_set_status_message("Search: %s", search_buf);
                buf_refresh_screen();
            }

            sstr_free(&E.search_query);
            E.search_query = sstr_from(search_buf, search_len);
            E.mode = MODE_NORMAL;
            buf_find();
            break;
    }
}

void ed_init(void) {
    E.num_buffers = 0;
    E.current_buffer = 0;
    E.render_x = 0;
    E.screen_rows = 0;
    E.screen_cols = 0;
    E.status_msg[0] = '\0';
    E.command_len = 0;
    E.mode = MODE_NORMAL;  /* Direct assignment to avoid firing hook during init */
    E.clipboard = sstr_new();
    E.search_query = sstr_new();

    if (get_window_size(&E.screen_rows, &E.screen_cols) == -1) die("get_window_size");
    E.screen_rows -= 2; /* Status bar and message bar */

    /* Initialize hook system */
    hook_init();

    /* Initialize keybinding system */
    keybind_init();

    /* Initialize command system */
    command_init();

    /* Create initial empty buffer */
    buf_new(NULL);
}
