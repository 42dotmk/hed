#include "hed.h"
#include <dirent.h>
#include <limits.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

Ed E;

/* --- Command-line (:) file path completion --- */
static void cmdcomp_clear(void) {
    if (E.cmd_complete.items) {
        for (int i = 0; i < E.cmd_complete.count; i++)
            free(E.cmd_complete.items[i]);
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
        if (E.command_buf[i] == ' ') {
            start = i + 1;
            break;
        }
    }
    int rlen = (int)strlen(replacement);
    if (start + rlen >= (int)sizeof(E.command_buf))
        rlen = (int)sizeof(E.command_buf) - 1 - start;
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
        if (E.command_buf[i] == ' ') {
            start = i + 1;
            break;
        }
    }
    char token[PATH_MAX];
    int tlen = len - start;
    if (tlen < 0)
        tlen = 0;
    if (tlen > (int)sizeof(token) - 1)
        tlen = (int)sizeof(token) - 1;
    memcpy(token, E.command_buf + start, (size_t)tlen);
    token[tlen] = '\0';
    /* Only start completion if token begins with '.', '~', or '/' */
    if (tlen == 0)
        return;
    char first = token[0];
    if (!(first == '.' || first == '~' || first == '/'))
        return;
    char full[PATH_MAX];
    if (token[0] == '~' && home) {
        if (token[1] == '/' || token[1] == '\0')
            snprintf(full, sizeof(full), "%s/%s", home,
                     token[1] ? token + 2 - 1 : "");
        else
            snprintf(full, sizeof(full), "%s", token); /* unsupported ~user */
    } else {
        snprintf(full, sizeof(full), "%s", token);
    }
    const char *slash = strrchr(full, '/');
    char base[PATH_MAX];
    char pref[PATH_MAX];
    if (slash) {
        size_t blen = (size_t)(slash - full + 1);
        if (blen >= sizeof(base))
            blen = sizeof(base) - 1;
        memcpy(base, full, blen);
        base[blen] = '\0';
        snprintf(pref, sizeof(pref), "%s", slash + 1);
    } else {
        base[0] = '\0';
        snprintf(pref, sizeof(pref), "%s", full);
    }
    DIR *d = opendir(base[0] ? base : ".");
    if (!d)
        return;
    struct dirent *de;
    int cap = 0;
    int count = 0;
    char **items = NULL;
    while ((de = readdir(d)) != NULL) {
        const char *name = de->d_name;
        if (name[0] == '.' && pref[0] != '.')
            continue;
        if (strncmp(name, pref, strlen(pref)) != 0)
            continue;
        int isdir = 0;
#ifdef DT_DIR
        if (de->d_type == DT_DIR)
            isdir = 1;
        if (de->d_type == DT_UNKNOWN)
#endif
        {
            char path[PATH_MAX];
            snprintf(path, sizeof(path), "%s%s", base[0] ? base : "", name);
            struct stat st;
            if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
                isdir = 1;
        }
        char cand[PATH_MAX];
        /* Include base path so tokens like "src/" complete to "src/<entry>" */
        snprintf(cand, sizeof(cand), "%s%s%s", base[0] ? base : "", name,
                 isdir ? "/" : "");
        if (count + 1 > cap) {
            cap = cap ? cap * 2 : 16;
            char **new_items = realloc(items, (size_t)cap * sizeof(char *));
            if (!new_items) {
                /* OOM: cleanup and abort completion */
                for (int i = 0; i < count; i++)
                    free(items[i]);
                free(items);
                closedir(d);
                return;
            }
            items = new_items;
        }
        char *cand_copy = strdup(cand);
        if (!cand_copy) {
            /* OOM: cleanup and abort completion */
            for (int i = 0; i < count; i++)
                free(items[i]);
            free(items);
            closedir(d);
            return;
        }
        items[count++] = cand_copy;
    }
    closedir(d);
    if (count == 0) {
        free(items);
        return;
    }
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
    if (!E.cmd_complete.active || E.cmd_complete.count == 0) {
        cmdcomp_build();
        return;
    }
    E.cmd_complete.index = (E.cmd_complete.index + 1) % E.cmd_complete.count;
    cmdcomp_apply_token(E.cmd_complete.items[E.cmd_complete.index]);
}

void ed_change_cursor_shape(void) {
    switch (E.mode) {
    case MODE_NORMAL:
        write(STDOUT_FILENO, CURSOR_STYLE_BLOCK, 5);
        break;
    case MODE_INSERT:
        write(STDOUT_FILENO, CURSOR_STYLE_BEAM, 5);
        break;
    case MODE_COMMAND:
    case MODE_VISUAL:
    case MODE_VISUAL_BLOCK:
        write(STDOUT_FILENO, CURSOR_STYLE_BLOCK, 5);
        break;
    }
}

void ed_set_mode(EditorMode new_mode) {
    if (E.mode == new_mode)
        return;

    EditorMode old_mode = E.mode;
    E.mode = new_mode;

    /* Clear visual selection when leaving a visual mode */
    if ((old_mode == MODE_VISUAL || old_mode == MODE_VISUAL_BLOCK) &&
        !(new_mode == MODE_VISUAL || new_mode == MODE_VISUAL_BLOCK)) {
        Window *win = window_cur();
        if (win) {
            win->sel.type = SEL_NONE;
        }
    }

    /* Clear keybind buffer when changing modes */
    keybind_clear_buffer();

    /* Notify undo system for grouping (e.g., end insert runs) */
    undo_on_mode_change(old_mode, new_mode);

    /* Fire mode change hook */
    HookModeEvent event = {old_mode, new_mode};
    hook_fire_mode(HOOK_MODE_CHANGE, &event);
}

static int command_tmux_history_nav(int direction) {
    const char *cmd = "tmux_send";
    size_t plen = strlen(cmd);
    if ((size_t)E.command_len < plen)
        return 0;
    if (strncmp(E.command_buf, cmd, plen) != 0)
        return 0;
    if ((size_t)E.command_len > plen && E.command_buf[plen] != ' ')
        return 0;

    const char *args = E.command_buf + plen;
    if (*args == ' ')
        args++;
    int args_len = E.command_len - (int)(args - E.command_buf);

    char candidate[512];
    int ok =
        (direction < 0)
            ? tmux_history_browse_up(args, args_len, candidate,
                                     (int)sizeof(candidate))
            : tmux_history_browse_down(candidate, (int)sizeof(candidate), NULL);
    if (!ok)
        return 0;

    int n = snprintf(E.command_buf, sizeof(E.command_buf), "tmux_send%s%s",
                     candidate[0] ? " " : "", candidate);
    if (n < 0)
        n = 0;
    if (n >= (int)sizeof(E.command_buf))
        n = (int)sizeof(E.command_buf) - 1;
    E.command_buf[n] = '\0';
    E.command_len = n;
    return 1;
}

int ed_read_key(void) {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN)
            die("read");
    }

    if (c == '\x1b') {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1)
            return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1)
                    return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                    case '3':
                        return KEY_DELETE;
                    case '5':
                        return KEY_PAGE_UP;
                    case '6':
                        return KEY_PAGE_DOWN;
                    }
                }
            } else {
                switch (seq[1]) {
                case 'A':
                    return KEY_ARROW_UP;
                case 'B':
                    return KEY_ARROW_DOWN;
                case 'C':
                    return KEY_ARROW_RIGHT;
                case 'D':
                    return KEY_ARROW_LEFT;
                case 'H':
                    return KEY_HOME;
                case 'F':
                    return KEY_END;
                }
            }
        }

        return '\x1b';
    } else {
        return c;
    }
}

void ed_move_cursor(int key) {
    (void)key;
    buf_move_cursor_key(key);
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
        *space = '\0'; /* Split command name and args */
        cmd_args = space + 1;
    }

    /* Execute command */
    log_msg(":%s%s%s", cmd_name, cmd_args ? " " : "", cmd_args ? cmd_args : "");
    if (!command_execute(cmd_name, cmd_args)) {
        if (space)
            *space = ' ';
        ed_set_status_message("Unknown command: %s", E.command_buf);
    } else {
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
        tmux_history_reset_browse();
        cmdcomp_clear();
    }
}

/* Mode-specific keypress handlers (refactored for clarity and maintainability)
 */

static void handle_command_mode_keypress(int c) {
    if (c == '\r') {
        ed_process_command();
    } else if (c == '\x1b') {
        E.mode = MODE_NORMAL;
        E.command_len = 0;
        hist_reset_browse(&E.history);
        tmux_history_reset_browse();
        cmdcomp_clear();
    } else if (c == KEY_DELETE || c == CTRL_KEY('h')) {
        if (E.command_len > 0)
            E.command_len--;
        hist_reset_browse(&E.history);
        tmux_history_reset_browse();
        cmdcomp_clear();
    } else if (c == KEY_ARROW_UP) {
        if (command_tmux_history_nav(-1)) {
            cmdcomp_clear();
        } else {
            tmux_history_reset_browse();
            if (hist_browse_up(&E.history, E.command_buf, E.command_len,
                               E.command_buf, (int)sizeof(E.command_buf))) {
                E.command_len = (int)strlen(E.command_buf);
            } else {
                ed_set_status_message("No history match");
            }
            cmdcomp_clear();
        }
    } else if (c == KEY_ARROW_DOWN) {
        int restored = 0;
        if (command_tmux_history_nav(1)) {
            cmdcomp_clear();
        } else {
            tmux_history_reset_browse();
            if (hist_browse_down(&E.history, E.command_buf,
                                 (int)sizeof(E.command_buf), &restored)) {
                E.command_len = (int)strlen(E.command_buf);
            }
            cmdcomp_clear();
        }
    } else if (c == '\t') {
        cmdcomp_next();
    } else if (!iscntrl(c) && c < 128) {
        if (E.command_len < (int)sizeof(E.command_buf) - 1) {
            E.command_buf[E.command_len++] = c;
        }
        hist_reset_browse(&E.history);
        tmux_history_reset_browse();
        cmdcomp_clear();
    }
}

static void handle_insert_mode_keypress(int c, Buffer *buf) {
    if (!buf)
        return;
    if (keybind_process(c, E.mode))
        return;
    if (!iscntrl(c)) {
        buf_insert_char_in(buf, c);
    }
}

/*
 * Interactive search prompt.
 * Reads a search query from the user and executes the search in the current
 * buffer. Handles Enter (execute), Escape (cancel), and backspace during input.
 */
static void ed_start_search(Buffer *buf) {
    if (!PTR_VALID(buf))
        return;

    /* Save current mode and enter command mode for visual feedback */
    EditorMode saved_mode = E.mode;
    ed_set_mode(MODE_COMMAND);
    E.command_len = 0;
    ed_set_status_message("Search: ");
    ed_render_frame();

    /* Read search query interactively */
    int search_len = 0;
    char search_buf[80];

    while (1) {
        int k = ed_read_key();

        if (k == '\r') {
            /* Execute search */
            break;
        }

        if (k == '\x1b') {
            /* Cancel search */
            ed_set_mode(saved_mode);
            return;
        }

        if (k == KEY_DELETE && search_len > 0) {
            search_len--;
        } else if (!iscntrl(k) && k >= 0 && search_len < 79) {
            search_buf[search_len++] = (char)k;
        }

        search_buf[search_len] = '\0';
        ed_set_status_message("Search: %s", search_buf);
        ed_render_frame();
    }

    /* Update global search query and find in buffer */
    sstr_free(&E.search_query);
    E.search_query = sstr_from(search_buf, search_len);
    E.mode = saved_mode;
    buf_find_in(buf);
}

void ed_search_prompt(void) {
    Buffer *buf = buf_cur();
    if (!buf)
        return;
    ed_start_search(buf);
}

static void handle_normal_mode_keypress(int c, Buffer *buf) {
    (void)buf;
    keybind_process(c, E.mode);
}

static void handle_visual_mode_keypress(int c, Buffer *buf, int block_mode) {
    (void)buf;
    (void)block_mode;
    if (!keybind_process(c, E.mode)) {
        keybind_process(c, MODE_NORMAL);
    }
}

/* Main keypress dispatcher - delegates to mode-specific handlers */
void ed_process_keypress(void) {
    int c = ed_read_key();
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    int old_x = win ? win->cursor.x : 0;
    int old_y = win ? win->cursor.y : 0;

    /* Dispatch to appropriate mode handler */
    switch (E.mode) {
    case MODE_COMMAND:
        handle_command_mode_keypress(c);
        break;
    case MODE_INSERT:
        handle_insert_mode_keypress(c, buf);
        break;
    case MODE_NORMAL:
        handle_normal_mode_keypress(c, buf);
        break;
    case MODE_VISUAL:
        handle_visual_mode_keypress(c, buf, 0);
        break;
    case MODE_VISUAL_BLOCK:
        handle_visual_mode_keypress(c, buf, 1);
        break;
    }

    /* Fire cursor-move hook if cursor changed position */
    /* some command may have changed the win and buf, so we need to get them again */
    win = window_cur();
    buf = buf_cur();
    if (buf && win && (win->cursor.x != old_x || win->cursor.y != old_y)) {
        HookCursorEvent ev = {buf, old_x, old_y, win->cursor.x, win->cursor.y};
        hook_fire_cursor(HOOK_CURSOR_MOVE, &ev);
    }
}
void ed_init_state() {
    E.buffers.data = NULL;
    E.buffers.len = 0;
    E.buffers.cap = 0;
    vec_reserve_typed(&E.buffers, BUFFERS_INITIAL_CAP, sizeof(Buffer));
    E.current_buffer = 0;
    E.windows.data = NULL;
    E.windows.len = 0;
    E.windows.cap = 0;
    vec_reserve_typed(&E.windows, WINDOWS_INITIAL_CAP, sizeof(Window));
    E.render_x = 0;
    E.screen_rows = 0;
    E.screen_cols = 0;
    E.status_msg[0] = '\0';
    E.command_len = 0;
    E.mode = MODE_NORMAL;
    E.stay_in_command = 0;
    E.show_line_numbers = 0;
    E.relative_line_numbers = 0;
    E.default_wrap = 0;
    E.expand_tab = 0;
    E.tab_size = TAB_STOP;
    E.clipboard_is_block = 0;
    E.cwd[0] = '\0';
    E.clipboard = sstr_new();
    E.search_query = sstr_new();
}

void ed_init(int create_default_buffer) {
    ed_init_state();

    /* Initialize editor working directory to process CWD at startup. */
    if (!getcwd(E.cwd, sizeof(E.cwd))) {
        E.cwd[0] = '\0';
    }

    if (get_window_size(&E.screen_rows, &E.screen_cols) == -1)
        die("get_window_size");
    E.screen_rows -= 2; /* Status bar and message bar */
    qf_init(&E.qf);
    regs_init();
    undo_init();
    undo_set_cap(4 * 1024 * 1024);
    hook_init();
    command_init();
    keybind_init();
    hist_init(&E.history);
    recent_files_init(&E.recent_files);
    jump_list_init(&E.jump_list);

    /* Ensure at least one editable buffer exists at startup if requested */
    if (create_default_buffer) {
        int empty_idx = -1;
        if (buf_new(NULL, &empty_idx) == ED_OK) {
            E.current_buffer = empty_idx;
        }
    }

    windows_init();
    E.wlayout_root = wlayout_init_root(0);
}
