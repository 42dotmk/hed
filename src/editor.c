#include "editor.h"
#include "input.h"
#include "hooks.h"
#include "prompt.h"
#include "select_loop.h"
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
#include "lib/path_limits.h"
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

    int key = ed_parse_key_from_fd(STDIN_FILENO);

    if (macro_is_recording()) {
        int should_record = 1;
        if (E.mode == MODE_NORMAL && (key == 'q' || key == '@')) {
            should_record = 0;
        }
        /* Bracketed paste markers are not user keystrokes — never
         * record them, and the paste body is read raw inside
         * ed_handle_paste() so it doesn't pass through here. */
        if (key == KEY_PASTE_START || key == KEY_PASTE_END) {
            should_record = 0;
        }
        if (should_record) {
            macro_record_key(key);
        }
    }

    return key;
}

/* Drain a bracketed paste body (between ESC[200~ and ESC[201~) and
 * insert it verbatim into the current buffer, bypassing keymap
 * dispatch, HOOK_KEYPRESS, and HOOK_CHAR_INSERT. That's what makes
 * pastes survive auto-pair / smart-indent / numeric prefixes
 * unmangled. The whole paste lands as one undo group. */
static void ed_handle_paste(void) {
    Buffer *buf = buf_cur();

    /* Paste only makes sense when there's a buffer to receive text.
     * If there isn't one, still drain bytes until the end marker so
     * leftover content doesn't bleed into subsequent keystrokes. */
    int can_insert = (buf != NULL);

    /* Force INSERT mode for the duration so the paste lands as text
     * regardless of whether the user was in NORMAL/VISUAL/etc.
     * Restore afterwards. */
    EditorMode prev_mode = E.mode;
    if (can_insert && E.mode != MODE_INSERT) ed_set_mode(MODE_INSERT);

    if (can_insert) undo_begin(buf, "paste");

    /* End-marker matcher: ESC '[' '2' '0' '1' '~'.
     * If the match fails partway through, the consumed prefix has to
     * be re-emitted as literal text (paste content is allowed to
     * contain arbitrary bytes, including stray ESC). */
    static const char END_MARKER[6] = { '\x1b', '[', '2', '0', '1', '~' };
    int matched = 0;

    for (;;) {
        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n == 0) break;            /* EOF — terminal closed */
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            break;
        }

        if (c == END_MARKER[matched]) {
            matched++;
            if (matched == (int)sizeof(END_MARKER)) break;
            continue;
        }

        /* Mismatch: flush any matched prefix as literal text, then
         * re-process the current byte. Done iteratively so a stray
         * ESC mid-paste doesn't drop characters. */
        if (matched > 0) {
            for (int i = 0; i < matched; i++) {
                char p = END_MARKER[i];
                if (can_insert) {
                    if (p == '\n' || p == '\r')
                        buf_insert_newline_in(buf);
                    else if ((unsigned char)p >= 0x20 || p == '\t')
                        buf_insert_char_in(buf, (unsigned char)p);
                    /* Other control bytes (incl. ESC) are dropped to
                     * keep pasted ANSI sequences from corrupting the
                     * file. */
                }
            }
            matched = 0;
            /* Fall through to handle `c` itself below. */
            if (c == END_MARKER[0]) { matched = 1; continue; }
        }

        if (!can_insert) continue;
        if (c == '\n' || c == '\r') {
            buf_insert_newline_in(buf);
        } else if ((unsigned char)c >= 0x20 || c == '\t') {
            buf_insert_char_in(buf, (unsigned char)c);
        }
        /* Drop other control bytes silently. */
    }

    if (can_insert) undo_end(buf);
    if (can_insert && prev_mode != MODE_INSERT) ed_set_mode(prev_mode);
}


static void handle_edit_mode_keypress(int c) {
    if (keybind_process(c, E.mode))
        return;
    switch (E.mode) {
    case MODE_INSERT:
        if (!iscntrl(c)) {
            BUFWIN(buf, win);
            buf_insert_char_in(buf, c);
            HookCharEvent event = {buf, win->cursor.x, win->cursor.y, c};
            hook_fire_char(HOOK_CHAR_INSERT, &event);
        }
        return;
    case MODE_VISUAL_LINE:
    case MODE_VISUAL_BLOCK:
        if (keybind_process(c, MODE_VISUAL))
            return;
        /* fallthrough */
    case MODE_VISUAL:
    case MODE_NORMAL:
        keybind_process(c, MODE_NORMAL);

        return;

    case MODE_COMMAND:
        return;
    }
}

/* Per-mode dispatch for one key. Public so plugins (e.g., multicursor)
 * can replay a key at multiple cursors without re-firing HOOK_KEYPRESS. */
void ed_dispatch_key(int c) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();

    if (buf && E.mode != MODE_INSERT && undo_has_open(buf))
        undo_end(buf);

    int old_x = win ? win->cursor.x : 0;
    int old_y = win ? win->cursor.y : 0;

    if (prompt_active())
        prompt_handle_key(c);
    else
        handle_edit_mode_keypress(c);

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
    if (c == KEY_PASTE_START) {
        ed_handle_paste();
        return;
    }
    /* Stray end marker (e.g. terminal sent it without a start, or we
     * lost sync somehow) — drop it rather than dispatching as a key. */
    if (c == KEY_PASTE_END) return;
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
    E.mode = MODE_NORMAL;
    E.show_line_numbers = 0;
    E.relative_line_numbers = 0;
    E.default_wrap = 0;
    E.expand_tab = 0;
    E.tab_size = TAB_STOP;
    E.cwd[0] = '\0';
    E.search_query = sstr_new();
    E.search_is_regex = 1;
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
    ed_loop_init();

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
