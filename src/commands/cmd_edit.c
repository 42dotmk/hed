/* Editing, motion and mode commands backing the default keymaps.
 * Most bodies were converted from keybinding-only callbacks in
 * input/keybinds_builtins.c so keymaps can cmap onto them. */

#include "commands/cmd_edit.h"
#include "buf/buf_helpers.h"
#include "editor.h"
#include "fs/fs.h"
#include "input/command_mode.h"
#include "input/keybinds.h"
#include "input/keybinds_builtins.h"
#include "input/picker.h"
#include "lib/args.h"
#include "lib/safe_string.h"
#include "lib/strutil.h"
#include "terminal.h"
#include "utils/yank.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*** Line / char edits ***/

void cmd_delete_line(const char *args) {
    (void)args;
    ASSERT_EDIT(buf, win);

    TextSelection sel;
    if (!textobj_line_with_newline(buf, win->cursor.y, win->cursor.x, &sel))
        return;

    buf_delete_selection(&sel);
}

void cmd_delete_char(const char *args) {
    (void)args;
    ASSERT_EDIT(buf, win);
    TextSelection sel;
    if (!textobj_char_at_cursor(buf, win->cursor.y, win->cursor.x, &sel))
        return;

    buf_delete_selection(&sel);
}

void cmd_delete_eol(const char *args) {
    (void)args;
    ASSERT_EDIT(buf, win);

    TextSelection sel;
    if (!textobj_to_line_end(buf, win->cursor.y, win->cursor.x, &sel))
        return;

    buf_delete_selection(&sel);
    win->cursor.x = sel.start.col > 0 ? sel.start.col - 1 : 0;
}

void cmd_change_line(const char *args) {
    (void)args;
    buf_change_line();
}

void cmd_change_eol(const char *args) {
    (void)args;
    buf_change_to_line_end();
}

void cmd_yank_line(const char *args) {
    (void)args;
    kb_yank_line();
}

void cmd_join(const char *args) {
    (void)args;
    buf_join_lines();
}

void cmd_indent(const char *args) {
    (void)args;
    buf_indent_line();
}

void cmd_move_line_up(const char *args) {
    (void)args;
    buf_move_line_up();
}

void cmd_move_line_down(const char *args) {
    (void)args;
    buf_move_line_down();
}

void cmd_duplicate_line(const char *args) {
    (void)args;
    buf_duplicate_line();
}

/* Delete the active selection, else the given text object. Backs the
 * VSCode Del / Ctrl+Backspace / Ctrl+Del binds: the objects include
 * the newline at line edges, so deleting there joins lines. */
static void delete_obj_or_selection(TextObjFunc fn) {
    ASSERT_EDIT(buf, win);
    if (win->sel.type != SEL_NONE) {
        kb_visual_delete_selection();
        return;
    }
    TextSelection sel;
    if (!fn(buf, win->cursor.y, win->cursor.x, &sel))
        return;
    buf_delete_selection(&sel);
}

void cmd_delete_forward(const char *args) {
    (void)args;
    delete_obj_or_selection(textobj_char_forward);
}

void cmd_delete_word_left(const char *args) {
    (void)args;
    delete_obj_or_selection(textobj_word_run_back);
}

void cmd_delete_word_right(const char *args) {
    (void)args;
    delete_obj_or_selection(textobj_word_run_fwd);
}

/* :extend <motion> [count] — enter visual mode if needed (anchor at
 * the cursor), then move, growing the selection: the command form of
 * Shift+motion in the modeless keymaps. <motion> is any registered
 * text object — including the word-name aliases left/right/up/down/
 * pageup/pagedown. */
void cmd_extend(const char *args) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!buf || !win || buf->num_rows == 0)
        return;

    char motion[32];
    args = args_skip_ws(args_next_token(args, motion, sizeof(motion)));
    if (!motion[0]) {
        ed_set_status_message("Usage: :extend <motion> [count]");
        return;
    }
    int count = 1;
    if (*args) {
        char *end;
        long c = strtol(args, &end, 10);
        if (end != args && c >= 1)
            count = (int)c;
    }

    int began = 0;
    if (!kb_in_visual()) {
        kb_visual_begin(0);
        began = 1;
    }

    int ty = win->cursor.y, tx = win->cursor.x;
    for (int i = 0; i < count; i++) {
        TextSelection sel;
        if (!textobj_lookup(motion, buf, ty, tx, &sel)) {
            if (began)
                kb_visual_escape();
            ed_set_status_message(":extend: unknown motion '%s'", motion);
            return;
        }
        ty = sel.cursor.line;
        tx = sel.cursor.col;
    }
    win->cursor.y = ty;
    win->cursor.x = tx;
}

/* :select_line — select the current line; repeating extends the
 * selection one line at a time (VSCode Ctrl+L semantics). */
void cmd_select_line(const char *args) {
    (void)args;
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!buf || !win || buf->num_rows == 0)
        return;
    if (!kb_in_visual()) {
        win->cursor.x = 0;
        kb_visual_begin(0);
    }
    if (win->cursor.y < buf->num_rows - 1) {
        win->cursor.y++;
        win->cursor.x = 0;
    } else {
        kb_goto_line_end();
    }
}

void cmd_unindent(const char *args) {
    (void)args;
    buf_unindent_line();
}

/* Toggle case of character under cursor and move right */
void cmd_toggle_case(const char *args) {
    (void)args;
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

void cmd_toggle_comment(const char *args) {
    (void)args;
    buf_toggle_comment();
}

/* Replace the char under the cursor — or every char of an active
 * visual selection — with the char in args, or the next typed key. */
void cmd_replace_char(const char *args) {
    int c;
    if (args && *args) {
        c = (unsigned char)args[0];
    } else {
        ed_set_status_message("r: char?");
        c = ed_read_key();
        if (c == '\x1b') {
            ed_set_status_message("");
            return;
        }
    }
    if (c == '\r' || c == '\n') {
        ed_set_status_message("Cannot replace with newline");
        return;
    }
    Window *win = window_cur();
    if (win && win->sel.type != SEL_NONE)
        kb_visual_replace_char_apply(c);
    else
        kb_replace_char_apply(c);
}

/*** View ***/

void cmd_center(const char *args) {
    (void)args;
    buf_center_screen();
}

void cmd_scrollup(const char *args) {
    (void)args;
    buf_scroll_half_page_up();
}

void cmd_scrolldown(const char *args) {
    (void)args;
    buf_scroll_half_page_down();
}

/* Scroll the viewport one line (vim C-e/C-y, VSCode Ctrl+Down/Up):
 * move row_offset, dragging the cursor along only as far as needed to
 * keep it visible. */
static void scroll_line(int dir) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!buf || !win || buf->num_rows == 0)
        return;
    win->row_offset += dir;
    if (win->row_offset > buf->num_rows - 1)
        win->row_offset = buf->num_rows - 1;
    if (win->row_offset < 0)
        win->row_offset = 0;
    if (win->cursor.y < win->row_offset)
        win->cursor.y = win->row_offset;
    if (win->height > 0 && win->cursor.y >= win->row_offset + win->height)
        win->cursor.y = win->row_offset + win->height - 1;
    int len = (int)buf->rows[win->cursor.y].chars.len;
    if (win->cursor.x > len)
        win->cursor.x = len;
}

void cmd_scroll_line_up(const char *args) {
    (void)args;
    scroll_line(-1);
}

void cmd_scroll_line_down(const char *args) {
    (void)args;
    scroll_line(+1);
}

/*** Search / navigation ***/

void cmd_search(const char *args) {
    (void)args;
    ed_search_prompt();
}

void cmd_search_next(const char *args) {
    (void)args;
    Buffer *buf = buf_cur();
    if (!buf)
        return;
    kb_jump_save_current();
    buf_find_in(buf);
}

void cmd_search_prev(const char *args) {
    (void)args;
    Buffer *buf = buf_cur();
    if (!buf)
        return;
    kb_jump_save_current();
    buf_find_prev_in(buf);
}

void cmd_search_word(const char *args) {
    (void)args;
    StrView w;
    if (!buf_word_view_under_cursor(&w)) {
        return;
    }
    strbuf_free(&E.search_query);
    E.search_query = strbuf_from_view(w);
    ed_set_status_message("* %.*s", (int)(w.len > 40 ? 40 : w.len), w.data);
    kb_jump_save_current();
    buf_find_in(buf_cur());
}

/* Visual `*`: search for the selected text (single-line only, literal
 * match). Exits visual mode before jumping so the search motion
 * doesn't extend the selection. */
void cmd_search_selection(const char *args) {
    (void)args;
    BUFWIN(buf, win)
    if (buf->num_rows == 0 || win->sel.type != SEL_VISUAL)
        return;

    TextSelection ts;
    if (!kb_visual_to_textsel(buf, win, 0, &ts))
        return;
    int sy = ts.start.line, sx = ts.start.col;
    int ey = ts.end.line, ex = ts.end.col;
    if (sy != ey) {
        ed_set_status_message("*: multi-line selection not supported");
        return;
    }
    Row *row = &buf->rows[sy];
    if (ex > (int)row->chars.len)
        ex = (int)row->chars.len;
    if (ex <= sx) {
        ed_set_status_message("*: empty selection");
        return;
    }

    strbuf_free(&E.search_query);
    E.search_query = strbuf_from(row->chars.data + sx, (size_t)(ex - sx));
    E.search_is_regex = 0;

    kb_visual_escape();
    ed_set_status_message(
        "* %.*s", (int)(E.search_query.len > 40 ? 40 : E.search_query.len),
        E.search_query.data);
    kb_jump_save_current();
    buf_find_in(buf);
}

void cmd_match_bracket(const char *args) {
    (void)args;
    buf_find_matching_bracket();
}

/* Helper: perform jump in specified direction */
static void jump_go(int direction) {
    int cursor_x = 0, cursor_y = 0;
    char *filename = NULL;
    int success;

    if (direction < 0)
        success =
            jump_list_backward(&E.jump_list, &filename, &cursor_x, &cursor_y);
    else
        success =
            jump_list_forward(&E.jump_list, &filename, &cursor_x, &cursor_y);

    if (!success || !filename || !filename[0]) {
        free(filename);
        ed_set_status_message(direction < 0 ? "Already at oldest jump"
                                            : "Already at newest jump");
        return;
    }

    buf_open_or_switch(filename, false);
    free(filename);

    /* Restore cursor position */
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (buf) {
        int row = (cursor_y < buf->num_rows) ? cursor_y : buf->num_rows - 1;
        if (row < 0)
            row = 0;
        buf->cursor->y = row;
        buf->cursor->x = cursor_x;
        if (win) {
            win->cursor.y = row;
            win->cursor.x = cursor_x;
        }
    }
}

void cmd_jump_back(const char *args) {
    (void)args;
    jump_go(-1);
}

void cmd_jump_forward(const char *args) {
    (void)args;
    jump_go(1);
}

void cmd_openpath(const char *args) {
    (void)args;
    StrBuf path = strbuf_new();
    int line = 0, col = 0;
    if (!buf_get_path_under_cursor(&path, &line, &col) || !path.data ||
        path.len == 0) {
        strbuf_free(&path);
        ed_set_status_message("gf: no path under cursor");
        return;
    }

    if (path.len >= PATH_MAX) {
        strbuf_free(&path);
        ed_set_status_message("gf: path too long");
        return;
    }

    /* URI-shaped target (mail://thread:…): hand it to the open pipeline
     * verbatim — a plugin's BUFFER_OPEN_PRE hook claims its scheme. */
    if (strstr(path.data, "://")) {
        char uri[PATH_MAX];
        safe_strcpy(uri, path.data, sizeof(uri));
        strbuf_free(&path);
        buf_open_or_switch(uri, true);
        return;
    }

    char expanded[PATH_MAX];
    str_expand_tilde(path.data, expanded, sizeof(expanded));

    char base[PATH_MAX];
    base[0] = '\0';
    fs_path_dirname_buf(buf_cur()->filename, base, sizeof(base));
    if (base[0] == '\0') {
        if (E.cwd[0]) {
            safe_strcpy(base, E.cwd, sizeof(base));
        } else {
            char cwd[PATH_MAX];
            if (fs_getcwd(cwd, sizeof(cwd))) {
                safe_strcpy(base, cwd, sizeof(base));
            }
        }
    }

    char resolved[PATH_MAX];
    const char *target = expanded;
    if (!fs_path_is_absolute(expanded) && base[0]) {
        if (!fs_path_join(resolved, sizeof(resolved), base, expanded)) {
            strbuf_free(&path);
            ed_set_status_message("gf: path too long");
            return;
        }
        target = resolved;
    }

    strbuf_free(&path);
    bool found = fs_is_file(target);
    if (!found && !fs_path_is_absolute(expanded)) {
        /* Fall back to CWD for relative paths */
        char cwd_resolved[PATH_MAX];
        const char *cwd = E.cwd[0] ? E.cwd : NULL;
        char tmp_cwd[PATH_MAX];
        if (!cwd && fs_getcwd(tmp_cwd, sizeof(tmp_cwd)))
            cwd = tmp_cwd;
        if (cwd &&
            fs_path_join(cwd_resolved, sizeof(cwd_resolved), cwd, expanded) &&
            fs_is_file(cwd_resolved)) {
            target = cwd_resolved;
            found = true;
        }
        if (!found) {
            ed_set_status_message("gf: file does not exist: %s", expanded);
            return;
        }
    } else if (!found) {
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

void cmd_searchpath(const char *args) {
    (void)args;
    StrBuf path = strbuf_new();
    if (!buf_get_path_under_cursor(&path, NULL, NULL) || !path.data ||
        path.len == 0) {
        strbuf_free(&path);
        ed_set_status_message("gF: no path under cursor");
        return;
    }

    char query[PATH_MAX];
    size_t copy_len = path.len;
    if (copy_len >= sizeof(query))
        copy_len = sizeof(query) - 1;
    memcpy(query, path.data, copy_len);
    query[copy_len] = '\0';
    strbuf_free(&path);

    if (!picker_invoke("files", query))
        ed_set_status_message("gF: no files picker installed");
}

/*** Mode switching ***/

static int in_visual_mode(void) {
    return E.mode == MODE_VISUAL || E.mode == MODE_VISUAL_LINE ||
           E.mode == MODE_VISUAL_BLOCK;
}

void cmd_insert(const char *args) {
    (void)args;
    Window *win = window_cur();
    if (win)
        kb_visual_clear(win);
    ed_set_mode(MODE_INSERT);
}

void cmd_append(const char *args) {
    (void)args;
    BUFWIN(buf, win)
    kb_visual_clear(win);
    ed_set_mode(MODE_INSERT);
    if (win->cursor.y < buf->num_rows) {
        Row *row = &buf->rows[win->cursor.y];
        if (win->cursor.x < (int)row->chars.len)
            win->cursor.x++;
    }
}

void cmd_insert_bol(const char *args) {
    (void)args;
    /* Move to start of line using text object, then enter insert mode */
    BUFWIN(buf, win)
    TextSelection sel;
    if (textobj_to_line_start(buf, win->cursor.y, win->cursor.x, &sel)) {
        win->cursor.y = sel.start.line;
        win->cursor.x = sel.start.col;
    }
    cmd_insert(NULL);
}

void cmd_append_eol(const char *args) {
    (void)args;
    /* Move to end of line using text object, then enter append mode */
    BUFWIN(buf, win)
    TextSelection sel;
    if (textobj_to_line_end(buf, win->cursor.y, win->cursor.x, &sel)) {
        win->cursor.y = sel.end.line;
        win->cursor.x = sel.end.col;
    }
    cmd_append(NULL);
}

void cmd_visual(const char *args) {
    (void)args;
    if (in_visual_mode())
        kb_visual_escape();
    else
        kb_visual_begin(0);
}

void cmd_visual_line(const char *args) {
    (void)args;
    BUFWIN(buf, win)
    if (E.mode == MODE_VISUAL_LINE && win->sel.type == SEL_VISUAL_LINE) {
        kb_visual_clear(win);
        ed_set_mode(MODE_NORMAL);
        return;
    }
    win->sel.type = SEL_VISUAL_LINE;
    win->sel.anchor_y = win->cursor.y;
    win->sel.anchor_x = win->cursor.x;
    win->sel.anchor_rx =
        buf_row_cx_to_rx(&buf->rows[win->cursor.y], win->cursor.x);
    ed_set_mode(MODE_VISUAL_LINE);
}

void cmd_visual_block(const char *args) {
    (void)args;
    if (E.mode == MODE_VISUAL_BLOCK)
        kb_visual_escape();
    else
        kb_visual_begin(1);
}

/*** Operators ***/

/* Resolve an operator's target text object: from args (":delete iw"),
 * or by reading 1-2 keys interactively under a status prompt. Returns
 * 0 = cancelled / unknown, 1 = *sel filled, 2 = doubled line form
 * (the second d of dd, c of cc, y of yy). */
static int operator_target(const char *args, int line_key, const char *prompt,
                           TextSelection *sel) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!buf || !win)
        return 0;

    if (args && *args) {
        while (*args == ' ' || *args == '\t')
            args++;
        if (*args &&
            textobj_lookup(args, buf, win->cursor.y, win->cursor.x, sel))
            return 1;
        ed_set_status_message("Unknown text object");
        return 0;
    }

    ed_set_status_message("%s", prompt);
    ed_render_frame();

    int key = ed_read_key();

    /* Cancel on escape */
    if (key == CTRL_KEY('[') || key == '\x1b') {
        ed_set_status_message("");
        return 0;
    }

    if (line_key && key == line_key)
        return 2;

    /* Try single-key text object first */
    char textobj_key[16];
    snprintf(textobj_key, sizeof(textobj_key), "%c", key);
    if (textobj_lookup(textobj_key, buf, win->cursor.y, win->cursor.x, sel))
        return 1;

    /* Try two-key text object (e.g., 'i' + 'w' = "iw") */
    int key2 = ed_read_key();
    snprintf(textobj_key, sizeof(textobj_key), "%c%c", key, key2);
    if (textobj_lookup(textobj_key, buf, win->cursor.y, win->cursor.x, sel))
        return 1;

    ed_set_status_message("Unknown text object");
    return 0;
}

void cmd_delete(const char *args) {
    Window *win = window_cur();
    if (win && win->sel.type != SEL_NONE) {
        kb_visual_delete_selection();
        return;
    }
    TextSelection sel;
    switch (operator_target(args, 'd', "-- DELETE --", &sel)) {
    case 2:
        cmd_delete_line(NULL);
        ed_set_status_message("Deleted line");
        break;
    case 1:
        buf_delete_selection(&sel);
        ed_set_status_message("Deleted");
        break;
    }
}

void cmd_change(const char *args) {
    TextSelection sel;
    switch (operator_target(args, 'c', "-- CHANGE --", &sel)) {
    case 2:
        buf_change_line();
        ed_set_status_message("");
        break;
    case 1:
        buf_change_selection(&sel);
        ed_set_status_message("");
        break;
    }
}

void cmd_yank(const char *args) {
    Window *win = window_cur();
    if (win && win->sel.type != SEL_NONE) {
        kb_visual_yank_selection();
        return;
    }
    TextSelection sel;
    switch (operator_target(args, 'y', "-- YANK --", &sel)) {
    case 2:
        kb_yank_line();
        ed_set_status_message("Yanked line");
        break;
    case 1:
        yank_selection(&sel);
        ed_set_status_message("Yanked");
        break;
    }
}

/* Apply a matched textobj to the visual selection anchored at the
 * origin: a motion (sel.cursor != origin) extends the selection to its
 * target — backward ones (gg, b, {) put the target in sel.cursor while
 * sel.end stays at the origin side, so sel.end must not be used here.
 * An object (iw, ip, i( — sel.cursor == origin) covers start..end. */
static void visual_apply_textobj(Buffer *buf, Window *win,
                                 const TextSelection *sel, int oy, int ox) {
    if (sel->cursor.line != oy || sel->cursor.col != ox) {
        win->cursor.y = sel->cursor.line;
        win->cursor.x = sel->cursor.col;
        return;
    }
    win->sel.anchor_y = sel->start.line;
    win->sel.anchor_x = sel->start.col;
    win->sel.anchor_rx =
        buf_row_cx_to_rx(&buf->rows[sel->start.line], sel->start.col);
    win->cursor.y = sel->end.line;
    /* sel->end is exclusive; the visual cursor is inclusive, so land
     * on the object's last character. */
    win->cursor.x = sel->end.col > 0 ? sel->end.col - 1 : 0;
}

void cmd_select(const char *args) {
    BUFWIN(buf, win)

    /* Enter visual mode and set anchor */
    win->sel.type = SEL_VISUAL;
    win->sel.anchor_y = win->cursor.y;
    win->sel.anchor_x = win->cursor.x;
    win->sel.anchor_rx =
        buf_row_cx_to_rx(&buf->rows[win->cursor.y], win->cursor.x);
    ed_set_mode(MODE_VISUAL);

    int oy = win->cursor.y, ox = win->cursor.x;
    TextSelection sel;
    if (operator_target(args, 0, "-- VISUAL --", &sel) != 1) {
        win->sel.type = SEL_NONE;
        ed_set_mode(MODE_NORMAL);
        return;
    }
    visual_apply_textobj(buf, win, &sel, oy, ox);
    ed_set_status_message("-- VISUAL --");
}
