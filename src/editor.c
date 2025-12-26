#include "hed.h"
#include "command_mode.h"
#include "macros.h"
#include <dirent.h>
#include <limits.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

Ed E;

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
	case MODE_VISUAL_LINE:
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

    if ((old_mode == MODE_VISUAL || old_mode == MODE_VISUAL_BLOCK) &&
        !(new_mode == MODE_VISUAL || new_mode == MODE_VISUAL_BLOCK)) {
        Window *win = window_cur();
        if (win) {
            win->sel.type = SEL_NONE;
        }
    }

    keybind_clear_buffer();
    HookModeEvent event = {old_mode, new_mode};
    hook_fire_mode(HOOK_MODE_CHANGE, &event);

}

int ed_read_key(void) {

    if (macro_queue_has_keys()) {
        return macro_queue_get_key();
    }

    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN)
            die("read");
    }

    int key = 0;

    if (c == '\x1b') {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) {
            key = '\x1b';
        } else if (read(STDIN_FILENO, &seq[1], 1) != 1) {
            key = '\x1b';
        } else if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) {
                    key = '\x1b';
                } else if (seq[2] == '~') {
                    switch (seq[1]) {
                    case '3':
                        key = KEY_DELETE;
                        break;
                    case '5':
                        key = KEY_PAGE_UP;
                        break;
                    case '6':
                        key = KEY_PAGE_DOWN;
                        break;
                    default:
                        key = '\x1b';
                        break;
                    }
                } else {
                    key = '\x1b';
                }
            } else {
                switch (seq[1]) {
                case 'A':
                    key = KEY_ARROW_UP;
                    break;
                case 'B':
                    key = KEY_ARROW_DOWN;
                    break;
                case 'C':
                    key = KEY_ARROW_RIGHT;
                    break;
                case 'D':
                    key = KEY_ARROW_LEFT;
                    break;
                case 'H':
                    key = KEY_HOME;
                    break;
                case 'F':
                    key = KEY_END;
                    break;
                default:
                    key = '\x1b';
                    break;
                }
            }
        } else {
            key = '\x1b';
        }
    } else {
        key = c;
    }

    if (macro_is_recording()) {
        int should_record = 1;
        if (E.mode == MODE_NORMAL && (key == 'q' || key == '@')) {
            should_record = 0;
        }
        if (should_record) {
            macro_record_key(key);
        }
    }

    return key;
}

void ed_move_cursor(int key) {
    (void)key;
    buf_move_cursor_key(key);
}

/* Mode-specific keypress handlers (refactored for clarity and maintainability)
 */

static void handle_insert_mode_keypress(int c) {
	BUFWIN(buf, win);
    if (keybind_process(c, E.mode))
        return;
    if (!iscntrl(c)) {
        buf_insert_char_in(buf, c);
	    HookCharEvent event = {buf, win->cursor.x, win->cursor.y, c};
	    hook_fire_char(HOOK_CHAR_INSERT, &event);
    }
}

static void handle_normal_mode_keypress(int c, Buffer *buf) {
    (void)buf;
    keybind_process(c, E.mode);
}

static void handle_visual_mode_keypress(int c, Buffer *buf) {
    (void)buf;
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
        command_mode_handle_keypress(c);
        break;
    case MODE_INSERT:
        handle_insert_mode_keypress(c);
        break;
    case MODE_NORMAL:
        handle_normal_mode_keypress(c, buf);
        break;
    case MODE_VISUAL:
    case MODE_VISUAL_LINE:
    case MODE_VISUAL_BLOCK:
        handle_visual_mode_keypress(c, buf);
        break;
    }

    /* Fire cursor-move hook if cursor changed position */
    /* some command may have changed the win and buf, so we need to get them
     * again */
    win = window_cur();
    buf = buf_cur();
    if (buf && win && (win->cursor.x != old_x || win->cursor.y != old_y)) {
        HookCursorEvent ev = {buf, old_x, old_y, win->cursor.x, win->cursor.y};
        hook_fire_cursor(HOOK_CURSOR_MOVE, &ev);
    }
}


void ed_init_state() {
    E.current_buffer = 0;
    E.modal_window = NULL;
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
    E.cwd[0] = '\0';
    E.search_query = sstr_new();
    E.search_is_regex = 1;
    E.search_prompt_active = 0;
}

void ed_init(int create_default_buffer) {
    log_msg("Initializing editor state");
    ed_init_state();
    log_msg("Editor state initialized");

    if (!getcwd(E.cwd, sizeof(E.cwd))) {
        E.cwd[0] = '\0';
    }

    if (get_window_size(&E.screen_rows, &E.screen_cols) == -1)
        die("get_window_size");
    E.screen_rows -= 2; /* Status bar and message bar */
    qf_init(&E.qf);
    regs_init();
    hook_init();
    command_init();
    keybind_init();
    hist_init(&E.history);
    recent_files_init(&E.recent_files);
    jump_list_init(&E.jump_list);
    macro_init();

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
