#include "hed.h"
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>

Ed E;

/* --- Command-line (:) file path completion --- */
static void cmdcomp_clear(void) {
    if (E.cmd_complete.items) {
        for (int i = 0; i < E.cmd_complete.count; i++) free(E.cmd_complete.items[i]);
        free(E.cmd_complete.items);
    }
    E.cmd_complete.items = NULL;
    E.cmd_complete.count = 0;
    E.cmd_complete.index = 0;
    E.cmd_complete.base[0] = '\0';
    E.cmd_complete.prefix[0] = '\0';
    E.cmd_complete.active = 0;
}

static void cmdcomp_apply_token(const char *replacement) {
    int len = E.command_len;
    int start = 0;
    for (int i = len - 1; i >= 0; i--) {
        if (E.command_buf[i] == ' ') { start = i + 1; break; }
    }
    int rlen = (int)strlen(replacement);
    if (start + rlen >= (int)sizeof(E.command_buf)) rlen = (int)sizeof(E.command_buf) - 1 - start;
    memcpy(E.command_buf + start, replacement, (size_t)rlen);
    E.command_len = start + rlen;
    E.command_buf[E.command_len] = '\0';
}

static void cmdcomp_build(void) {
    cmdcomp_clear();
    const char *home = getenv("HOME");
    int len = E.command_len;
    int start = 0;
    for (int i = len - 1; i >= 0; i--) {
        if (E.command_buf[i] == ' ') { start = i + 1; break; }
    }
    char token[PATH_MAX];
    int tlen = len - start; if (tlen < 0) tlen = 0; if (tlen > (int)sizeof(token) - 1) tlen = (int)sizeof(token) - 1;
    memcpy(token, E.command_buf + start, (size_t)tlen); token[tlen] = '\0';
    /* Only start completion if token begins with '.', '~', or '/' */
    if (tlen == 0) return;
    char first = token[0];
    if (!(first == '.' || first == '~' || first == '/')) return;
    char full[PATH_MAX];
    if (token[0] == '~' && home) {
        if (token[1] == '/' || token[1] == '\0') snprintf(full, sizeof(full), "%s/%s", home, token[1] ? token + 2 - 1 : "");
        else snprintf(full, sizeof(full), "%s", token); /* unsupported ~user */
    } else {
        snprintf(full, sizeof(full), "%s", token);
    }
    const char *slash = strrchr(full, '/');
    char base[PATH_MAX];
    char pref[PATH_MAX];
    if (slash) {
        size_t blen = (size_t)(slash - full + 1);
        if (blen >= sizeof(base)) blen = sizeof(base) - 1;
        memcpy(base, full, blen); base[blen] = '\0';
        snprintf(pref, sizeof(pref), "%s", slash + 1);
    } else {
        base[0] = '\0'; snprintf(pref, sizeof(pref), "%s", full);
    }
    DIR *d = opendir(base[0] ? base : ".");
    if (!d) return;
    struct dirent *de;
    int cap = 0; int count = 0; char **items = NULL;
    while ((de = readdir(d)) != NULL) {
        const char *name = de->d_name;
        if (name[0] == '.' && pref[0] != '.') continue;
        if (strncmp(name, pref, strlen(pref)) != 0) continue;
        int isdir = 0;
#ifdef DT_DIR
        if (de->d_type == DT_DIR) isdir = 1;
        if (de->d_type == DT_UNKNOWN)
#endif
        {
            char path[PATH_MAX];
            snprintf(path, sizeof(path), "%s%s", base[0] ? base : "", name);
            struct stat st; if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) isdir = 1;
        }
        char cand[PATH_MAX];
        /* Include base path so tokens like "src/" complete to "src/<entry>" */
        snprintf(cand, sizeof(cand), "%s%s%s", base[0] ? base : "", name, isdir ? "/" : "");
        if (count + 1 > cap) { cap = cap ? cap * 2 : 16; items = realloc(items, (size_t)cap * sizeof(char*)); }
        items[count++] = strdup(cand);
    }
    closedir(d);
    if (count == 0) { free(items); return; }
    E.cmd_complete.items = items;
    E.cmd_complete.count = count;
    E.cmd_complete.index = 0;
    snprintf(E.cmd_complete.base, sizeof(E.cmd_complete.base), "%s", base);
    snprintf(E.cmd_complete.prefix, sizeof(E.cmd_complete.prefix), "%s", pref);
    E.cmd_complete.active = 1;
    cmdcomp_apply_token(items[0]);
    ed_set_status_message("%d matches", count);
}

static void cmdcomp_next(void) {
    if (!E.cmd_complete.active || E.cmd_complete.count == 0) { cmdcomp_build(); return; }
    E.cmd_complete.index = (E.cmd_complete.index + 1) % E.cmd_complete.count;
    cmdcomp_apply_token(E.cmd_complete.items[E.cmd_complete.index]);
}

/* Command history moved to history.c */

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

    /* Notify undo system for grouping (e.g., end insert runs) */
    undo_on_mode_change(old_mode, new_mode);

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
    } else {
        /* Successful command: record to history and ':' register */
        regs_set_cmd(E.command_buf, strlen(E.command_buf));
        hist_add(&E.history, E.command_buf);
    }

    if (E.stay_in_command) {
        E.stay_in_command = 0; /* consume flag: remain in command mode */
        /* do not clear command buffer; command likely prefilled it */
        E.mode = MODE_COMMAND;
    } else {
        ed_set_mode(MODE_NORMAL);
        E.command_len = 0;
        hist_reset_browse(&E.history);
        cmdcomp_clear();
    }
}

void ed_process_keypress(void) {
    int c = ed_read_key();
    Buffer *buf = buf_cur();

    /* Quickfix focus input */
    if (E.qf.open && E.qf.focus && E.mode != MODE_COMMAND) {
        if (c == 'j' || c == 1003) { /* Down */
            qf_move(&E.qf, 1);
            return;
        } else if (c == 'k' || c == 1002) { /* Up */
            qf_move(&E.qf, -1);
            return;
        } else if (c == '\r') {
            qf_open_selected(&E.qf);
            return;
        } else if (c == 'q' || c == '\x1b') {
            E.qf.focus = 0; /* keep pane open */
            return;
        }
    }

    if (E.mode == MODE_COMMAND) {
        if (c == '\r') {
            ed_process_command();
        } else if (c == '\x1b') {
            E.mode = MODE_NORMAL;
            E.command_len = 0;
            hist_reset_browse(&E.history);
            cmdcomp_clear();
        } else if (c == 127 || c == CTRL_KEY('h')) {
            if (E.command_len > 0) E.command_len--;
            hist_reset_browse(&E.history);
            cmdcomp_clear();
        } else if (c == 1002) { /* Up */
            if (hist_browse_up(&E.history, E.command_buf, E.command_len,
                               E.command_buf, (int)sizeof(E.command_buf))) {
                E.command_len = (int)strlen(E.command_buf);
            } else {
                ed_set_status_message("No history match");
            }
            cmdcomp_clear();
        } else if (c == 1003) { /* Down */
            int restored = 0;
            if (hist_browse_down(&E.history, E.command_buf, (int)sizeof(E.command_buf), &restored)) {
                E.command_len = (int)strlen(E.command_buf);
            }
            cmdcomp_clear();
        } else if (c == '\t') {
            cmdcomp_next();
        } else if (!iscntrl(c) && c < 128) {
            if (E.command_len < (int)sizeof(E.command_buf) - 1) {
                E.command_buf[E.command_len++] = c;
            }
            hist_reset_browse(&E.history); /* typing resets browse */
            cmdcomp_clear();
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
    E.stay_in_command = 0;
    E.show_line_numbers = 0;
    E.relative_line_numbers = 0;
    E.clipboard = sstr_new();
    E.search_query = sstr_new();
    qf_init(&E.qf);

    if (get_window_size(&E.screen_rows, &E.screen_cols) == -1) die("get_window_size");
    E.screen_rows -= 2; /* Status bar and message bar */

    /* Initialize registers */
    regs_init();

    /* Initialize undo system (4MB cap) */
    undo_init();
    undo_set_cap(4 * 1024 * 1024);

    /* Initialize hook system */
    hook_init();

    /* Initialize keybinding system */
    keybind_init();

    /* Initialize command system */
    command_init();

    /* Load command history */
    hist_init(&E.history);

    /* Create initial empty buffer */
    buf_new(NULL);
}
