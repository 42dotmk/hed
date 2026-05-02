#include "editor.h"
#include "hooks.h"
#include "keybinds.h"
#include "terminal.h"
#include "buf/buf_helpers.h"
#include "registers.h"
#include "commands.h"
#include "ui/wlayout.h"
#include "lib/log.h"
#include <ctype.h>
#include <errno.h>
#include "command_mode.h"
#include "config.h"
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

/* Modeless support: when 1, any attempt to enter NORMAL mode is silently
 * redirected to INSERT. Set by emacs/vscode-style keymaps so the user
 * never sits in normal mode. */
static int g_modeless = 0;

void ed_set_modeless(int on) {
    g_modeless = on ? 1 : 0;
    if (g_modeless && E.mode == MODE_NORMAL) {
        ed_set_mode(MODE_INSERT);
    }
}

int ed_is_modeless(void) { return g_modeless; }

void ed_set_mode(EditorMode new_mode) {
    /* Redirect NORMAL → INSERT when modeless. */
    if (g_modeless && new_mode == MODE_NORMAL)
        new_mode = MODE_INSERT;

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
            /* Bare ESC. */
            key = '\x1b';
        } else if (seq[0] == 'O') {
            /* SS3 sequence: ESC O <letter> for F1-F4 (xterm) and some
             * Home/End forms. */
            char letter;
            if (read(STDIN_FILENO, &letter, 1) != 1) {
                key = KEY_META | 'O';
            } else {
                switch (letter) {
                case 'P': key = KEY_F1; break;
                case 'Q': key = KEY_F2; break;
                case 'R': key = KEY_F3; break;
                case 'S': key = KEY_F4; break;
                case 'H': key = KEY_HOME; break;
                case 'F': key = KEY_END; break;
                default:  key = '\x1b'; break;
                }
            }
        } else if (seq[0] != '[') {
            /* ESC followed by any non-CSI byte = Meta/Alt + that key.
             * Terminals encode M-x as the two bytes ESC, 'x'. */
            key = KEY_META | (unsigned char)seq[0];
        } else if (read(STDIN_FILENO, &seq[1], 1) != 1) {
            /* CSI prefix but no follow-up — degrade to bare ESC. */
            key = '\x1b';
        } else {
            /* seq[0] == '[': CSI escape sequence. */
            if (seq[1] >= '0' && seq[1] <= '9') {
                /* Read full digit run, then ';' (modifier) or '~' (terminator). */
                char digits[8] = { seq[1] };
                int dlen = 1;
                char tail = '\0';
                int parse_ok = 1;
                while (dlen < (int)sizeof(digits) - 1) {
                    char c2;
                    if (read(STDIN_FILENO, &c2, 1) != 1) { parse_ok = 0; break; }
                    if (c2 >= '0' && c2 <= '9') {
                        digits[dlen++] = c2;
                        continue;
                    }
                    tail = c2;
                    break;
                }
                digits[dlen] = '\0';
                int n = atoi(digits);

                /* Map a CSI-numeric "~" code to a special key. */
                int base = 0;
                switch (n) {
                case 3:  base = KEY_DELETE;    break;
                case 5:  base = KEY_PAGE_UP;   break;
                case 6:  base = KEY_PAGE_DOWN; break;
                case 11: case 12: case 13: case 14:
                    base = KEY_F1 + (n - 11); break;
                case 15: base = KEY_F5;        break;
                case 17: case 18: case 19: case 20: case 21:
                    base = KEY_F6 + (n - 17); break;
                case 23: case 24:
                    base = KEY_F11 + (n - 23); break;
                case 1:  /* used only with ';<mod><letter>' suffix */
                    break;
                }

                if (!parse_ok) {
                    key = '\x1b';
                } else if (tail == '~') {
                    key = base ? base : '\x1b';
                } else if (tail == ';') {
                    /* Modifier follows: <mod><letter|~>.
                     * For n==1, letter encodes the key (A/B/C/D/H/F).
                     * For function keys, terminator is '~' and base
                     * comes from `n`. */
                    char mod_b, term;
                    if (read(STDIN_FILENO, &mod_b, 1) != 1 ||
                        read(STDIN_FILENO, &term, 1) != 1) {
                        key = '\x1b';
                    } else {
                        int b = 0;
                        if (n == 1) {
                            switch (term) {
                            case 'A': b = KEY_ARROW_UP;    break;
                            case 'B': b = KEY_ARROW_DOWN;  break;
                            case 'C': b = KEY_ARROW_RIGHT; break;
                            case 'D': b = KEY_ARROW_LEFT;  break;
                            case 'H': b = KEY_HOME;        break;
                            case 'F': b = KEY_END;         break;
                            }
                        } else if (term == '~') {
                            b = base;
                        }
                        if (b) {
                            int flags = 0;
                            switch (mod_b) {
                            case '2': flags = KEY_SHIFT; break;
                            case '3': flags = KEY_META; break;
                            case '4': flags = KEY_SHIFT | KEY_META; break;
                            case '5': flags = KEY_CTRL; break;
                            case '6': flags = KEY_SHIFT | KEY_CTRL; break;
                            case '7': flags = KEY_META | KEY_CTRL; break;
                            case '8': flags = KEY_SHIFT | KEY_META | KEY_CTRL; break;
                            }
                            key = flags | b;
                        } else {
                            key = '\x1b';
                        }
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
    /* Fall back through MODE_VISUAL so visual_line/visual_block only need
     * to register the keys whose behaviour actually differs. */
    if (keybind_process(c, E.mode))
        return;
    if ((E.mode == MODE_VISUAL_LINE || E.mode == MODE_VISUAL_BLOCK) &&
        keybind_process(c, MODE_VISUAL))
        return;
    keybind_process(c, MODE_NORMAL);
}

/* Per-mode dispatch for one key. Public so plugins (e.g., multicursor)
 * can replay a key at multiple cursors without re-firing HOOK_KEYPRESS. */
void ed_dispatch_key(int c) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();

    /* Close any undo group left open by the previous keypress. INSERT
     * keeps its group open across keypresses (closed by the mode hook
     * on Esc); every other mode collapses each command into one undo. */
    if (buf && E.mode != MODE_INSERT && undo_has_open(buf))
        undo_end(buf);

    int old_x = win ? win->cursor.x : 0;
    int old_y = win ? win->cursor.y : 0;

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

/* Main keypress dispatcher - delegates to mode-specific handlers */
void ed_process_keypress(void) {
    int c = ed_read_key();
    HookKeyEvent kev = { c, 0 };
    hook_fire_key(HOOK_KEYPRESS, &kev);
    if (kev.consumed) return;
    ed_dispatch_key(c);
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

    /* All subsystems are ready — let the user wire up their config. */
    config_init();

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
