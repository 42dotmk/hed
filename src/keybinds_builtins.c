#include "keybinds_builtins.h"
#include "cmd_misc.h"
#include "command_mode.h"
#include "commands/cmd_util.h"
#include "commands_ui.h"
#include "file_helpers.h"
#include "fold.h"
#include "hed.h"
#include "strutil.h"
#include "buf_helpers.h"
#include "dired.h"
#include <assert.h>
#include <limits.h>
#include <stdlib.h>

/* Internal buffer row helpers (non-public, defined in buffer.c) */
void buf_row_del_in(Buffer *buf, int at);

/* Visual selection helpers (local to keybinding implementations) */
static void visual_clear(Window *win) {
    if (!win)
        return; 
    win->sel.type = SEL_NONE;       
}

Selection kb_make_selection(Window *win, Buffer *buf,
                            SelectionType forced_type) {
    Selection s = (Selection){.type = SEL_NONE};
    if (!win || !buf)
        return s;
    if (forced_type != SEL_NONE) {
        s.type = forced_type;
    } else if (win->sel.type != SEL_NONE) {
        s = win->sel;
    }
    if (s.type == SEL_VISUAL) {
        s.anchor_y = win->sel.anchor_y;
        s.anchor_x = win->sel.anchor_x;
        s.cursor_y = win->cursor.y;
        s.cursor_x = win->cursor.x;
    } else if (s.type == SEL_VISUAL_BLOCK) {
        s.anchor_y = win->sel.anchor_y;
        s.cursor_y = win->cursor.y;
        s.anchor_rx = win->sel.anchor_rx;
        s.block_start_rx = win->sel.anchor_rx;
        s.block_end_rx =
            buf_row_cx_to_rx(&buf->rows[win->cursor.y], win->cursor.x);
        if (s.block_start_rx > s.block_end_rx) {
            int t = s.block_start_rx;
            s.block_start_rx = s.block_end_rx;
            s.block_end_rx = t;
        }
    } else if (s.type == SEL_VISUAL_LINE) {
        s.anchor_y = win->cursor.y;
        s.cursor_y = win->cursor.y;
    }
    return s;
}

static void visual_begin(int block) {
    BUFWIN(buf, win)
    win->sel.type = block ? SEL_VISUAL_BLOCK : SEL_VISUAL;
    win->sel.anchor_y = win->cursor.y;
    win->sel.anchor_x = win->cursor.x;
    win->sel.anchor_rx =
        buf_row_cx_to_rx(&buf->rows[win->cursor.y], win->cursor.x);
    win->sel.block_start_rx = win->sel.anchor_rx;
    win->sel.block_end_rx = win->sel.anchor_rx;
    ed_set_mode(block ? MODE_VISUAL_BLOCK : MODE_VISUAL);
}

static int visual_char_range(Buffer *buf, Window *win, int *sy, int *sx,
                             int *ey, int *ex_excl) {
    if (!buf || !win || win->sel.type != SEL_VISUAL)
        return 0;
    if (!BOUNDS_CHECK(win->sel.anchor_y, buf->num_rows) ||
        !BOUNDS_CHECK(win->cursor.y, buf->num_rows))
        return 0;
    int ay = win->sel.anchor_y, ax = win->sel.anchor_x;
    int cy = win->cursor.y, cx = win->cursor.x;
    int top_y = ay, top_x = ax, bot_y = cy, bot_x = cx;
    if (ay > cy || (ay == cy && ax > cx)) {
        top_y = cy;
        top_x = cx;
        bot_y = ay;
        bot_x = ax;
    }
    if (top_y < 0)
        top_y = 0;
    if (bot_y >= buf->num_rows)
        bot_y = buf->num_rows - 1;
    Row *top_row = &buf->rows[top_y];
    Row *bot_row = &buf->rows[bot_y];
    if (top_x > (int)top_row->chars.len)
        top_x = (int)top_row->chars.len;
    if (bot_x > (int)bot_row->chars.len)
        bot_x = (int)bot_row->chars.len;
    if (sy)
        *sy = top_y;
    if (sx)
        *sx = top_x;
    if (ey)
        *ey = bot_y;
    if (ex_excl)
        *ex_excl = bot_x + 1;
    return 1;
}

static int visual_block_range(Buffer *buf, Window *win, int *sy, int *ey,
                              int *start_rx, int *end_rx_excl) {
    if (!buf || !win || win->sel.type != SEL_VISUAL_BLOCK)
        return 0;
    if (!BOUNDS_CHECK(win->sel.anchor_y, buf->num_rows) ||
        !BOUNDS_CHECK(win->cursor.y, buf->num_rows))
        return 0;
    int top_y =
        win->sel.anchor_y < win->cursor.y ? win->sel.anchor_y : win->cursor.y;
    int bot_y =
        win->sel.anchor_y > win->cursor.y ? win->sel.anchor_y : win->cursor.y;
    int cur_rx = buf_row_cx_to_rx(&buf->rows[win->cursor.y], win->cursor.x);
    int start = win->sel.anchor_rx < cur_rx ? win->sel.anchor_rx : cur_rx;
    int end = win->sel.anchor_rx > cur_rx ? win->sel.anchor_rx : cur_rx;
    if (sy)
        *sy = top_y;
    if (ey)
        *ey = bot_y;
    if (start_rx)
        *start_rx = start;
    if (end_rx_excl)
        *end_rx_excl = end + 1;
    return 1;
}

static int visual_yank(Buffer *buf, Window *win, int block_mode) {
    if (!buf || !win || win->sel.type == SEL_NONE)
        return 0;
    if (block_mode != 0)
        block_mode = 1;
    if (win->sel.type == SEL_VISUAL_BLOCK)
        block_mode = 1;

    /* Convert visual selection to TextSelection */
    TextSelection sel;
    if (!block_mode) {
        int sy, sx, ey, ex_excl;
        if (!visual_char_range(buf, win, &sy, &sx, &ey, &ex_excl))
            return 0;
        sel = textsel_make_range(sy, sx, ey, ex_excl, SEL_VISUAL);
    } else {
        int sy, ey, start_rx, end_rx_excl;
        if (!visual_block_range(buf, win, &sy, &ey, &start_rx, &end_rx_excl))
            return 0;
        /* For block mode, convert render columns to character columns */
        Row *first_row = &buf->rows[sy];
        int sx = buf_row_rx_to_cx(first_row, start_rx);
        int ex = buf_row_rx_to_cx(first_row, end_rx_excl);
        sel = textsel_make_range(sy, sx, ey, ex, SEL_VISUAL_BLOCK);
    }

    /* Use new yank API */
    EdError err = yank_selection(&sel);
    if (err == ED_OK) {
        ed_set_status_message("Yanked");
        return 1;
    }
    return 0;
}

static int visual_delete(Buffer *buf, Window *win, int block_mode) {
    if (!buf || !win || win->sel.type == SEL_NONE)
        return 0;
    if (buf->readonly) {
        ed_set_status_message("Buffer is read-only");
        return 0;
    }
    if (block_mode != 0)
        block_mode = 1;
    if (win->sel.type == SEL_VISUAL_BLOCK)
        block_mode = 1;

    /* Convert visual selection to TextSelection */
    TextSelection sel;
    if (!block_mode) {
        int sy, sx, ey, ex_excl;
        if (!visual_char_range(buf, win, &sy, &sx, &ey, &ex_excl))
            return 0;
        sel = textsel_make_range(sy, sx, ey, ex_excl, SEL_VISUAL);
    } else {
        int sy, ey, start_rx, end_rx_excl;
        if (!visual_block_range(buf, win, &sy, &ey, &start_rx, &end_rx_excl))
            return 0;
        /* For block mode, convert render columns to character columns */
        Row *first_row = &buf->rows[sy];
        int sx = buf_row_rx_to_cx(first_row, start_rx);
        int ex = buf_row_rx_to_cx(first_row, end_rx_excl);
        sel = textsel_make_range(sy, sx, ey, ex, SEL_VISUAL_BLOCK);
    }

    /* Yank first (to clipboard) */
    EdError err = yank_selection(&sel);
    if (err != ED_OK)
        return 0;

    /* Delete the selection */
    buf_delete_selection(&sel);

    visual_clear(win);
    ed_set_mode(MODE_NORMAL);
    return 1;
}

/* Expose small wrappers for other modules */
void kb_visual_clear(Window *win) { visual_clear(win); }
void kb_visual_begin(int block) { visual_begin(block); }
int kb_visual_yank(Buffer *buf, Window *win, int block_mode) {
    return visual_yank(buf, win, block_mode);
}
int kb_visual_delete(Buffer *buf, Window *win, int block_mode) {
    return visual_delete(buf, win, block_mode);
}

static int kb_visual_is_block_mode(void) { return E.mode == MODE_VISUAL_BLOCK; }

void kb_visual_yank_selection(void) {
    BUFWIN(buf, win)
    if (kb_visual_yank(buf, win, kb_visual_is_block_mode())) {
        kb_visual_clear(win);
        ed_set_mode(MODE_NORMAL);
    }
}

void kb_visual_delete_selection(void) {
    BUFWIN(buf, win)
    kb_visual_delete(buf, win, kb_visual_is_block_mode());
}

void kb_visual_escape(void) {
    BUFWIN(buf, win)
    kb_visual_clear(win);
    ed_set_mode(MODE_NORMAL);
}

void kb_visual_toggle_block_mode(void) {
    if (E.mode == MODE_VISUAL_BLOCK) {
        kb_visual_escape();
    } else {
        kb_visual_begin(1);
    }
}

void kb_visual_enter_insert_mode(void) {
    BUFWIN(buf, win)
    kb_visual_clear(win);
    kb_enter_insert_mode();
}

void kb_visual_enter_append_mode(void) {
    BUFWIN(buf, win)
    kb_visual_clear(win);
    kb_append_mode();
}

void kb_visual_enter_command_mode(void) {
    BUFWIN(buf, win)
    kb_visual_clear(win);
    kb_enter_command_mode();
}

/*** Default keybinding callbacks ***/

/* Normal mode - mode switching */
void kb_enter_insert_mode(void) { ed_set_mode(MODE_INSERT); }

void kb_append_mode(void) {
    BUFWIN(buf, win)
    ed_set_mode(MODE_INSERT);
    if (win->cursor.y < buf->num_rows) {
        Row *row = &buf->rows[win->cursor.y];
        if (win->cursor.x < (int)row->chars.len)
            win->cursor.x++;
    }
}

void kb_enter_command_mode(void) {
    extern Ed E;
    ed_set_mode(MODE_COMMAND);
    E.command_len = 0;
}

void kb_visual_toggle(void) {
    BUFWIN(buf, win)
    if (E.mode == MODE_VISUAL && win->sel.type == SEL_VISUAL) {
        visual_clear(win);
        ed_set_mode(MODE_NORMAL);
        return;
    }
    visual_begin(0);
}

void kb_visual_block_toggle(void) {
	BUFWIN(buf, win)
    if (E.mode == MODE_VISUAL_BLOCK && win->sel.type == SEL_VISUAL_BLOCK) {
        visual_clear(win);
        ed_set_mode(MODE_NORMAL);
        return;
    }
    visual_begin(1);
}

/* Normal mode - text operations */
void kb_delete_line(void) {
    ASSERT_EDIT(buf, win);

    TextSelection sel;
    if (!textobj_line_with_newline(buf, win->cursor.y, win->cursor.x, &sel))
        return;

    buf_delete_selection(&sel);
}

void kb_delete_to_line_end(void) {
    ASSERT_EDIT(buf, win);

    TextSelection sel;
    if (!textobj_to_line_end(buf, win->cursor.y, win->cursor.x, &sel))
        return;

    buf_delete_selection(&sel);
    win->cursor.x = sel.start.col > 0 ? sel.start.col - 1 : 0;
}

void kb_yank_line(void) {
	BUFWIN(buf, win)
    buf_yank_line_in(buf);
    ed_set_status_message("Yanked");
}

void kb_paste(void) {
	BUFWIN(buf, win)
    paste_from_register(buf, '"', true);
}

/* ========================================================================
 * Operator Functions (blocking text object composition)
 * ======================================================================== */

/* Helper: build text object key sequence from one or two keys */
static void build_textobj_key(char *buf, size_t size, int key1, int key2) {
    if (key2 == 0) {
        snprintf(buf, size, "%c", key1);
    } else {
        snprintf(buf, size, "%c%c", key1, key2);
    }
}

/* Delete operator - waits for text object input */
void kb_operator_delete(void) {
    BUFWIN(buf, win)

    ed_set_status_message("-- DELETE --");
    ed_render_frame();

    int key = ed_read_key();

    /* Cancel on escape */
    if (key == CTRL_KEY('[') || key == '\x1b') {
        ed_set_status_message("");
        return;
    }

    /* Special case: dd (delete line) */
    if (key == 'd') {
        kb_delete_line();
        ed_set_status_message("Deleted line");
        return;
    }

    /* Try single-key text object first */
    char textobj_key[16];
    TextSelection sel;

    build_textobj_key(textobj_key, sizeof(textobj_key), key, 0);
    if (textobj_lookup(textobj_key, buf, win->cursor.y, win->cursor.x, &sel)) {
        buf_delete_selection(&sel);
        ed_set_status_message("Deleted");
        return;
    }

    /* Try two-key text object (e.g., 'i' + 'w' = "iw") */
    int key2 = ed_read_key();
    build_textobj_key(textobj_key, sizeof(textobj_key), key, key2);
    if (textobj_lookup(textobj_key, buf, win->cursor.y, win->cursor.x, &sel)) {
        buf_delete_selection(&sel);
        ed_set_status_message("Deleted");
        return;
    }

    ed_set_status_message("Unknown text object");
}

/* Change operator - waits for text object input */
void kb_operator_change(void) {
    BUFWIN(buf, win)

    ed_set_status_message("-- CHANGE --");
    ed_render_frame();

    int key = ed_read_key();

    if (key == CTRL_KEY('[') || key == '\x1b') {
        ed_set_status_message("");
        return;
    }

    /* Special case: cc (change line) */
    if (key == 'c') {
        buf_change_line();
        ed_set_status_message("");
        return;
    }

    char textobj_key[16];
    TextSelection sel;

    build_textobj_key(textobj_key, sizeof(textobj_key), key, 0);
    if (textobj_lookup(textobj_key, buf, win->cursor.y, win->cursor.x, &sel)) {
        buf_change_selection(&sel);
        ed_set_status_message("");
        return;
    }

    int key2 = ed_read_key();
    build_textobj_key(textobj_key, sizeof(textobj_key), key, key2);
    if (textobj_lookup(textobj_key, buf, win->cursor.y, win->cursor.x, &sel)) {
        buf_change_selection(&sel);
        ed_set_status_message("");
        return;
    }

    ed_set_status_message("Unknown text object");
}

/* Yank operator - waits for text object input */
void kb_operator_yank(void) {
    BUFWIN(buf, win)

    ed_set_status_message("-- YANK --");
    ed_render_frame();

    int key = ed_read_key();

    if (key == CTRL_KEY('[') || key == '\x1b') {
        ed_set_status_message("");
        return;
    }

    /* Special case: yy (yank line) */
    if (key == 'y') {
        kb_yank_line();
        ed_set_status_message("Yanked line");
        return;
    }

    char textobj_key[16];
    TextSelection sel;

    build_textobj_key(textobj_key, sizeof(textobj_key), key, 0);
    if (textobj_lookup(textobj_key, buf, win->cursor.y, win->cursor.x, &sel)) {
        yank_selection(&sel);
        ed_set_status_message("Yanked");
        return;
    }

    int key2 = ed_read_key();
    build_textobj_key(textobj_key, sizeof(textobj_key), key, key2);
    if (textobj_lookup(textobj_key, buf, win->cursor.y, win->cursor.x, &sel)) {
        yank_selection(&sel);
        ed_set_status_message("Yanked");
        return;
    }

    ed_set_status_message("Unknown text object");
}

/* Move operator - moves cursor to text object position (fallback for unmapped keys) */
void kb_operator_move(int key) {
    BUFWIN(buf, win)

    /* Try single-key text object */
    char textobj_key[16];
    TextSelection sel;

    build_textobj_key(textobj_key, sizeof(textobj_key), key, 0);
    if (textobj_lookup(textobj_key, buf, win->cursor.y, win->cursor.x, &sel)) {
        /* Move cursor to the text object's cursor position */
        win->cursor.y = sel.cursor.line;
        win->cursor.x = sel.cursor.col;
        return;
    }

    /* Not a text object - do nothing (key not handled) */
}

/* Select operator - creates visual selection via text object (v + motion) */
void kb_operator_select(void) {
    BUFWIN(buf, win)

    /* Enter visual mode and set anchor */
    win->sel.type = SEL_VISUAL;
    win->sel.anchor_y = win->cursor.y;
    win->sel.anchor_x = win->cursor.x;
    win->sel.anchor_rx = buf_row_cx_to_rx(&buf->rows[win->cursor.y], win->cursor.x);
    ed_set_mode(MODE_VISUAL);

    ed_set_status_message("-- VISUAL --");
    ed_render_frame();

    int key = ed_read_key();

    if (key == CTRL_KEY('[') || key == '\x1b') {
        /* Cancel visual mode */
        win->sel.type = SEL_NONE;
        ed_set_mode(MODE_NORMAL);
        ed_set_status_message("");
        return;
    }

    /* Try single-key text object */
    char textobj_key[16];
    TextSelection sel;

    build_textobj_key(textobj_key, sizeof(textobj_key), key, 0);
    if (textobj_lookup(textobj_key, buf, win->cursor.y, win->cursor.x, &sel)) {
        /* Move cursor to end of selection and stay in visual mode */
        win->cursor.y = sel.end.line;
        win->cursor.x = sel.end.col;
        ed_set_status_message("-- VISUAL --");
        return;
    }

    /* Try two-key text object */
    int key2 = ed_read_key();
    build_textobj_key(textobj_key, sizeof(textobj_key), key, key2);
    if (textobj_lookup(textobj_key, buf, win->cursor.y, win->cursor.x, &sel)) {
        /* Move cursor to end of selection and stay in visual mode */
        win->cursor.y = sel.end.line;
        win->cursor.x = sel.end.col;
        ed_set_status_message("-- VISUAL --");
        return;
    }

    /* Unknown text object - cancel visual mode */
    win->sel.type = SEL_NONE;
    ed_set_mode(MODE_NORMAL);
    ed_set_status_message("Unknown text object");
}

void kb_dired_enter(void) {
    dired_handle_enter();
}

void kb_dired_parent(void) {
    dired_handle_parent();
}

void kb_dired_home(void) {
    dired_handle_home();
}

void kb_dired_chdir(void) {
    dired_handle_chdir();
}

void kb_delete_char(void) {
    ASSERT_EDIT(buf, win);
    TextSelection sel;
    if (!textobj_char_at_cursor(buf, win->cursor.y, win->cursor.x, &sel))
        return;

    buf_delete_selection(&sel);
}

void kb_insert_newline(void) {
    BUFWIN(buf, win);
    buf_insert_newline_in(buf);
}

void kb_insert_tab(void) {
    BUFWIN(buf, win)
    int tabw = (E.tab_size > 0) ? E.tab_size : TAB_STOP;
    if (!E.expand_tab) {
        buf_insert_char_in(buf, '\t');
    } else {
        int cx = win->cursor.x;
        int spaces = tabw - (cx % tabw);
        for (int i = 0; i < spaces; i++) {
            buf_insert_char_in(buf, ' ');
        }
    }
}

void kb_insert_backspace(void) {
    BUFWIN(buf, win)
    buf_del_char_in(buf);
}

void kb_insert_escape(void) {
    BUFWIN(buf, win)
    ed_set_mode(MODE_NORMAL);
    if (buf && win && win->cursor.x > 0)
        win->cursor.x--;
}

void kb_search_prompt(void) { ed_search_prompt(); }

/* Normal mode - cursor movement */
void kb_move_left(void) { ed_move_cursor(KEY_ARROW_LEFT); }
void kb_move_right(void) { ed_move_cursor(KEY_ARROW_RIGHT); }
void kb_move_up(void) { ed_move_cursor(KEY_ARROW_UP); }
void kb_move_down(void) { ed_move_cursor(KEY_ARROW_DOWN); }

/* Normal mode - search */
void kb_search_next(void) {
    Buffer *buf = buf_cur();
    if (!buf)
        return;
    buf_find_in(buf);
}

void kb_find_under_cursor(void) {
    SizedStr w = sstr_new();
    if (!buf_get_word_under_cursor(&w)) {
        sstr_free(&w);
        return;
    }
    sstr_free(&E.search_query);
    E.search_query = sstr_from(w.data, w.len);
    ed_set_status_message("* %.*s", (int)(w.len > 40 ? 40 : w.len), w.data);
    sstr_free(&w);
    buf_find_in(buf_cur());
}
void kb_search_file_under_cursor(void) {
    SizedStr path = sstr_new();
    if (!buf_get_path_under_cursor(&path, NULL, NULL) || !path.data ||
        path.len == 0) {
        sstr_free(&path);
        ed_set_status_message("gF: no path under cursor");
        return;
    }

    char query[PATH_MAX];
    size_t copy_len = path.len;
    if (copy_len >= sizeof(query))
        copy_len = sizeof(query) - 1;
    memcpy(query, path.data, copy_len);
    query[copy_len] = '\0';
    sstr_free(&path);

    char esc_query[PATH_MAX * 2];
    shell_escape_single(query, esc_query, sizeof(esc_query));

    const char *find_files_cmd = "(command -v rg >/dev/null 2>&1 && rg --files "
                                 "|| find . -type f -print)";
    const char *preview =
        "command -v bat >/dev/null 2>&1 && bat --style=plain --color=always "
        "--line-range :200 {} || sed -n \"1,200p\" {} 2>/dev/null";

    char fzf_opts[4096];
    snprintf(fzf_opts, sizeof(fzf_opts),
             "--select-1 --exit-0 --query %s --preview '%s' "
             "--preview-window right,60%%,wrap",
             esc_query, preview);

    char **sel = NULL;
    int cnt = 0;
    if (!fzf_run_opts(find_files_cmd, fzf_opts, 0, &sel, &cnt) || cnt <= 0 ||
        !sel[0] || !sel[0][0]) {
        fzf_free(sel, cnt);
        ed_set_status_message("gF: no selection");
        return;
    }

    buf_open_or_switch(sel[0], true);
    fzf_free(sel, cnt);
}
void kb_open_file_under_cursor(void) {
    SizedStr path = sstr_new();
    int line = 0, col = 0;
    if (!buf_get_path_under_cursor(&path, &line, &col) || !path.data ||
        path.len == 0) {
        sstr_free(&path);
        ed_set_status_message("gf: no path under cursor");
        return;
    }

    if (path.len >= PATH_MAX) {
        sstr_free(&path);
        ed_set_status_message("gf: path too long");
        return;
    }

    char expanded[PATH_MAX];
    str_expand_tilde(path.data, expanded, sizeof(expanded));

    char base[PATH_MAX];
    base[0] = '\0';
    path_dirname_buf(buf_cur()->filename, base, sizeof(base));
    if (base[0] == '\0') {
        if (E.cwd[0]) {
            safe_strcpy(base, E.cwd, sizeof(base));
        } else {
            char cwd[PATH_MAX];
            if (getcwd(cwd, sizeof(cwd))) {
                safe_strcpy(base, cwd, sizeof(base));
            }
        }
    }

    char resolved[PATH_MAX];
    const char *target = expanded;
    if (!path_is_absolute(expanded) && base[0]) {
        if (!path_join_dir(resolved, sizeof(resolved), base, expanded)) {
            sstr_free(&path);
            ed_set_status_message("gf: path too long");
            return;
        }
        target = resolved;
    }

    sstr_free(&path);
    FILE *f = fopen(target, "r");
    if (f) {
        fclose(f);
    } else {
        ed_set_status_message("gf: file does not exist: %s", target);
        return;
    }
    buf_open_or_switch(target, true);

    if (line > 0 || col > 0) {
        Buffer *buf = buf_cur();
        Window *win = window_cur();
        if (buf && win) {
            if (line > 0)
                buf_goto_line(line);
            if (col > 0 && win->cursor.y < buf->num_rows) {
                int max = buf->rows[win->cursor.y].chars.len;
                int cx = col - 1;
                if (cx < 0)
                    cx = 0;
                if (cx > max)
                    cx = max;
                win->cursor.x = cx;
            }
        }
    }
}
void kb_line_number_toggle(void) { cmd_ln(NULL); }
/* Undo/Redo */
void kb_undo(void) {
    ed_set_status_message("Undo disabled");
}

void kb_redo(void) {
    ed_set_status_message("Redo disabled");
}

/* Helper: perform jump in specified direction */
static void kb_jump(int direction) {
    int cursor_x, cursor_y;
    char *filename = "\0";
    int success;

    if (direction < 0) {
        success =
            jump_list_backward(&E.jump_list, &filename, &cursor_x, &cursor_y);

        if (success && filename && filename[0] != '\0') {
            buf_open_or_switch(filename, false);
            free(filename);
        } else {
            log_msg("At beginning of jump list");
            return;
        }
    } else {
        success =
            jump_list_forward(&E.jump_list, &filename, &cursor_x, &cursor_y);

        if (success && filename && filename[0] != '\0') {
            buf_open_or_switch(filename, false);
            free(filename);
        } else {
            log_msg("At end of jump list");
            return;
        }
    }
}

void kb_jump_backward(void) { kb_jump(-1); }

void kb_jump_forward(void) { kb_jump(1); }

/* Tmux integration: send current paragraph to tmux runner pane */
void kb_tmux_send_line(void) {
    SizedStr para = sstr_new();
    if (!buf_get_paragraph_under_cursor(&para) || para.len == 0) {
        sstr_free(&para);
        ed_set_status_message("tmux: no paragraph to send");
        return;
    }

    char *cmd = malloc(para.len + 1);
    if (!cmd) {
        sstr_free(&para);
        ed_set_status_message("tmux: out of memory");
        return;
    }

    memcpy(cmd, para.data, para.len);
    cmd[para.len] = '\0';

    tmux_send_command(cmd);

    free(cmd);
    sstr_free(&para);
}

/* Toggle case of character under cursor and move right */
void kb_toggle_case(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!buf || !win)
        return;
    if (buf->readonly) {
        ed_set_status_message("Buffer is read-only");
        return;
    }
    if (win->cursor.y >= buf->num_rows)
        return;

    Row *row = &buf->rows[win->cursor.y];
    if (win->cursor.x >= (int)row->chars.len)
        return;

    char old_char = row->chars.data[win->cursor.x];
    char new_char = char_toggle_case(old_char);

    if (new_char != old_char) {
        row->chars.data[win->cursor.x] = new_char;
        buf_row_update(row);
        buf->dirty++;
    }

    /* Move cursor right (Vim behavior) */
    if (win->cursor.x < (int)row->chars.len - 1)
        win->cursor.x++;
}

/* Replace character under cursor with next typed char (stay in normal mode) */
void kb_replace_char(void) {
	ASSERT_EDIT(buf, win)
    Row *row = &buf->rows[win->cursor.y];
    if (win->cursor.x >= (int)row->chars.len)
        return;

    ed_set_status_message("r: char?");
    int c = ed_read_key();

    /* Cancel on ESC */
    if (c == '\x1b') {
        ed_set_status_message("");
        return;
    }

    /* Don't allow newline replacement */
    if (c == '\r' || c == '\n') {
        ed_set_status_message("Cannot replace with newline");
        return;
    }

    row->chars.data[win->cursor.x] = (char)c;
    buf_row_update(row);
    buf->dirty++;
    ed_set_status_message("");
}

/* Fold keybindings */

/* za - Toggle fold at cursor */
void kb_fold_toggle(void) {
	BUFWIN(buf, win)
    int line = win->cursor.y;
    if (fold_toggle_at_line(&buf->folds, line)) {
        int idx = fold_find_at_line(&buf->folds, line);
        if (idx >= 0) {
            bool collapsed = buf->folds.regions[idx].is_collapsed;
            ed_set_status_message("Fold %s", collapsed ? "closed" : "opened");
        }
    } else {
        ed_set_status_message("No fold at cursor");
    }
}

void kb_fold_open(void) {
	BUFWIN(buf, win);

    int line = win->cursor.y;
    if (fold_expand_at_line(&buf->folds, line)) {
        ed_set_status_message("Fold opened");
    } else {
        ed_set_status_message("No fold at cursor");
    }
}

void kb_fold_close(void) {
	BUFWIN(buf, win);
    int line = win->cursor.y;
    if (fold_collapse_at_line(&buf->folds, line)) {
        ed_set_status_message("Fold closed");
    } else {
        ed_set_status_message("No fold at cursor");
    }
}

void kb_fold_open_all(void) {
    Buffer *buf = buf_cur();
    if (!buf)
        return;

    int count = 0;
    for (int i = 0; i < buf->folds.count; i++) {
        if (buf->folds.regions[i].is_collapsed) {
            buf->folds.regions[i].is_collapsed = false;
            count++;
        }
    }
    ed_set_status_message("Opened %d fold%s", count, count == 1 ? "" : "s");
}

void kb_fold_close_all(void) {
    Buffer *buf = buf_cur();
    if (!buf)
        return;

    int count = 0;
    for (int i = 0; i < buf->folds.count; i++) {
        if (!buf->folds.regions[i].is_collapsed) {
            buf->folds.regions[i].is_collapsed = true;
            count++;
        }
    }
    ed_set_status_message("Closed %d fold%s", count, count == 1 ? "" : "s");
}

void kb_del_win(char direction);
void kb_del_up() { kb_del_win('k'); }
void kb_del_down() { kb_del_win('j'); }
void kb_del_left() { kb_del_win('h'); }
void kb_del_right() { kb_del_win('l'); }

void kb_end_append(void) {
    /* Move to end of line using text object, then enter append mode */
    BUFWIN(buf, win)
    TextSelection sel;
    if (textobj_to_line_end(buf, win->cursor.y, win->cursor.x, &sel)) {
        win->cursor.y = sel.end.line;
        win->cursor.x = sel.end.col;
    }
    kb_append_mode();
}

void kb_start_insert(void) {
    /* Move to start of line using text object, then enter insert mode */
    BUFWIN(buf, win)
    TextSelection sel;
    if (textobj_to_line_start(buf, win->cursor.y, win->cursor.x, &sel)) {
        win->cursor.y = sel.start.line;
        win->cursor.x = sel.start.col;
    }
    kb_enter_insert_mode();
}

void kb_del_win(char direction) {
    switch (direction) {
    case 'h':
        windows_focus_left();
        break;
    case 'j':
        windows_focus_down();
        break;
    case 'k':
        windows_focus_up();
        break;
    case 'l':
        windows_focus_right();
        break;
    }
    cmd_wclose(NULL);
}

