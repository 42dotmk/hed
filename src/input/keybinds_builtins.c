#include "input/keybinds_builtins.h"
#include "buf/buf_helpers.h"
#include "commands/cmd_edit.h"
#include "commands/commands_ui.h"
#include "editor.h"
#include "hooks.h"
#include "input/command_mode.h"
#include "input/keybinds.h"
#include "input/registers.h"
#include "lib/errors.h"
#include "lib/safe_string.h"
#include "lib/strutil.h"
#include "terminal.h"
#include "ui/wlayout.h"
#include "utils/undo.h"
#include "utils/yank.h"
#include <stdlib.h>
#include <string.h>

/* Visual selection helpers (local to keybinding implementations) */
static void visual_clear(Window *win) {
    if (!win)
        return;
    win->sel.type = SEL_NONE;
}

static void visual_begin(int block) {
    BUFWIN(buf, win)
    win->sel.type = block ? SEL_VISUAL_BLOCK : SEL_VISUAL;
    win->sel.anchor_y = win->cursor.y;
    win->sel.anchor_x = win->cursor.x;
    win->sel.anchor_rx =
        buf_row_cx_to_rx(&buf->rows[win->cursor.y], win->cursor.x);
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

/* Line-mode visual range: full rows from min(anchor_y,cursor_y) to max. */
static int visual_line_range(Buffer *buf, Window *win, int *sy, int *ey) {
    if (!buf || !win || win->sel.type != SEL_VISUAL_LINE)
        return 0;
    if (!BOUNDS_CHECK(win->sel.anchor_y, buf->num_rows) ||
        !BOUNDS_CHECK(win->cursor.y, buf->num_rows))
        return 0;
    int ay = win->sel.anchor_y, cy = win->cursor.y;
    int top = ay < cy ? ay : cy;
    int bot = ay > cy ? ay : cy;
    if (sy)
        *sy = top;
    if (ey)
        *ey = bot;
    return 1;
}

int kb_visual_to_textsel(Buffer *buf, Window *win, int block_mode,
                         TextSelection *out) {
    if (!buf || !win || !out || win->sel.type == SEL_NONE)
        return 0;
    if (block_mode != 0)
        block_mode = 1;
    if (win->sel.type == SEL_VISUAL_BLOCK)
        block_mode = 1;

    if (win->sel.type == SEL_VISUAL_LINE) {
        int sy, ey;
        if (!visual_line_range(buf, win, &sy, &ey))
            return 0;
        int ex = (int)buf->rows[ey].chars.len;
        *out = textsel_make_range(sy, 0, ey, ex, SEL_VISUAL_LINE);
        return 1;
    }
    if (!block_mode) {
        int sy, sx, ey, ex_excl;
        if (!visual_char_range(buf, win, &sy, &sx, &ey, &ex_excl))
            return 0;
        *out = textsel_make_range(sy, sx, ey, ex_excl, SEL_VISUAL);
        return 1;
    }
    int sy, ey, start_rx, end_rx_excl;
    if (!visual_block_range(buf, win, &sy, &ey, &start_rx, &end_rx_excl))
        return 0;
    /* For block mode, convert render columns to character columns */
    Row *first_row = &buf->rows[sy];
    int sx = buf_row_rx_to_cx(first_row, start_rx);
    int ex = buf_row_rx_to_cx(first_row, end_rx_excl);
    *out = textsel_make_range(sy, sx, ey, ex, SEL_VISUAL_BLOCK);
    return 1;
}

static int visual_yank(Buffer *buf, Window *win, int block_mode) {
    if (win->sel.type == SEL_VISUAL_BLOCK) {
        int sy, ey, s_rx, e_rx;
        if (!visual_block_range(buf, win, &sy, &ey, &s_rx, &e_rx))
            return 0;
        if (yank_block(buf, sy, ey, s_rx, e_rx) != ED_OK)
            return 0;
        ed_set_status_message("Yanked");
        return 1;
    }

    TextSelection sel;
    if (!kb_visual_to_textsel(buf, win, block_mode, &sel))
        return 0;

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

    /* Block delete: capture the rectangle into the delete registers,
     * then remove only the selected columns on each row — not the
     * lines between them. */
    if (win->sel.type == SEL_VISUAL_BLOCK) {
        int sy, ey, s_rx, e_rx;
        if (!visual_block_range(buf, win, &sy, &ey, &s_rx, &e_rx))
            return 0;
        yank_block_as_delete(buf, sy, ey, s_rx, e_rx);
        buf_delete_block(buf, sy, ey, s_rx, e_rx);
        win->cursor.y = sy;
        win->cursor.x = buf_row_rx_to_cx(&buf->rows[sy], s_rx);
        visual_clear(win);
        ed_set_mode(MODE_NORMAL);
        return 1;
    }

    TextSelection sel;
    if (!kb_visual_to_textsel(buf, win, block_mode, &sel))
        return 0;

    /* Delete the selection; the delete paths capture the removed text
     * into the delete registers ('1'-'9' + unnamed, '0' untouched). */
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

/* p in visual mode: replace the selection with the unnamed register.
 * The register content is snapshotted before the deletion (which
 * rotates the delete registers) and restored afterwards, so the same
 * content can be pasted over selection after selection. */
void kb_visual_paste(void) {
    BUFWIN(buf, win)
    if (win->sel.type == SEL_NONE) {
        paste_from_register(buf, '"', true);
        return;
    }
    if (buf->readonly) {
        ed_set_status_message("Buffer is read-only");
        return;
    }
    const StrBuf *reg = regs_get('"');
    if (!reg || reg->len == 0) {
        ed_set_status_message("Nothing to paste");
        return;
    }
    size_t plen = reg->len;
    char *pdata = malloc(plen);
    if (!pdata)
        return;
    memcpy(pdata, reg->data, plen);
    RegType pt = regs_get_type('"');

    /* Paste below instead of above when a line-wise selection reached
     * the end of the buffer — the deletion leaves the cursor on the
     * line above the removed block. */
    bool after = false;
    bool whole_buffer = false;

    if (win->sel.type == SEL_VISUAL_BLOCK) {
        int sy, ey, s_rx, e_rx;
        if (visual_block_range(buf, win, &sy, &ey, &s_rx, &e_rx)) {
            yank_block_as_delete(buf, sy, ey, s_rx, e_rx);
            buf_delete_block(buf, sy, ey, s_rx, e_rx);
            win->cursor.y = sy;
            win->cursor.x = buf_row_rx_to_cx(&buf->rows[sy], s_rx);
        }
    } else {
        TextSelection sel;
        if (kb_visual_to_textsel(buf, win, 0, &sel)) {
            if (sel.type == SEL_VISUAL_LINE) {
                whole_buffer =
                    (sel.start.line == 0 && sel.end.line == buf->num_rows - 1);
            }
            int sy = sel.start.line;
            buf_delete_selection_keep_spacing(&sel);
            if (sel.type == SEL_VISUAL_LINE && sy >= buf->num_rows)
                after = true;
        }
    }
    visual_clear(win);
    ed_set_mode(MODE_NORMAL);

    /* Restore the pasted content into unnamed (the deletion above
     * rotated it into '1'), then paste it where the selection was. */
    regs_set_unnamed_typed(pdata, plen, pt);
    paste_from_register(buf, '"', after);

    /* Replacing every line of the buffer leaves the placeholder empty
     * row the deletion inserted; drop it so the paste is exact. */
    if (whole_buffer && pt == REG_LINEWISE && buf->num_rows > 1 &&
        buf->rows[buf->num_rows - 1].chars.len == 0) {
        buf_delete_lines_in(buf, buf->num_rows - 1, buf->num_rows - 1);
        win->cursor.y = 0;
        win->cursor.x = 0;
    }

    free(pdata);
}

/*** Default keybinding callbacks ***/

void kb_enter_command_mode(void) { cmd_prompt_open(); }

void kb_yank_line(void) {
    BUFWIN(buf, win)
    buf_yank_line_in(buf);
    ed_set_status_message("Yanked");
}

/* Yank operator, kept as a C callback so plugins (clipboard) can wrap
 * it; the implementation is the :yank command. */
void kb_operator_yank(void) { cmd_yank(NULL); }

/* Save current position to the jump list (for within-file jumps). */
void kb_jump_save_current(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!buf || !buf->filename)
        return;
    int cx = win ? win->cursor.x : buf->cursor->x;
    int cy = win ? win->cursor.y : buf->cursor->y;
    jump_list_add(&E.jump_list, buf->filename, cx, cy);
}

/* Move operator - moves cursor to text object position (fallback for
 * unmapped keys) */
void kb_operator_move(int key) {
    BUFWIN(buf, win)

    char textobj_key[2] = {(char)key, '\0'};
    TextSelection sel;

    if (textobj_lookup(textobj_key, buf, win->cursor.y, win->cursor.x, &sel)) {
        /* Save jump if the movement crosses 5+ lines */
        if (abs(sel.cursor.line - win->cursor.y) >= 5)
            kb_jump_save_current();
        win->cursor.y = sel.cursor.line;
        win->cursor.x = sel.cursor.col;
        return;
    }
}

void kb_insert_newline(void) {
    BUFWIN(buf, win);
    buf_insert_newline_in(buf);
    /* Fire char-insert hook for '\n' so plugins like smart_indent run.
     * The non-newline insert path in editor.c skips control chars, so
     * '\n' would otherwise never reach HOOK_CHAR_INSERT. */
    HookCharEvent ev = {buf, win->cursor.x, win->cursor.y, '\n'};
    hook_fire_char(HOOK_CHAR_INSERT, &ev);
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

/* Normal mode - cursor movement */
void kb_move_left(void) { buf_move_cursor_key(KEY_ARROW_LEFT); }
void kb_move_right(void) { buf_move_cursor_key(KEY_ARROW_RIGHT); }
void kb_move_up(void) { buf_move_cursor_key(KEY_ARROW_UP); }
void kb_move_down(void) { buf_move_cursor_key(KEY_ARROW_DOWN); }

/* f/F argument: the target char typed after the motion key. Multibyte
 * chars arrive byte-at-a-time from ed_read_key, so continuation bytes
 * are collected into one codepoint. During a count-repeat burst (3fx)
 * only the first iteration prompts; later ones reuse the read. */
static char findchar_seq[8];
static int findchar_len = 0;
static int findchar_read(void) {
    if (keybind_motion_repeat_index() > 0)
        return findchar_len > 0;

    findchar_len = 0;
    int c = ed_read_key();
    /* Decoded special keys (arrows, F-keys, meta combos) are > 0xff
     * and cancel the motion. Raw high bytes come sign-extended from
     * the reader, so normalize before classifying. */
    if (c > 0xff)
        return 0;
    unsigned char b = (unsigned char)c;
    /* Printable bytes and tab only; Esc and control chars cancel. */
    if (b < 0x20 && b != '\t')
        return 0;
    findchar_seq[findchar_len++] = (char)b;
    int cont = 0;
    if ((b & 0xE0) == 0xC0)
        cont = 1;
    else if ((b & 0xF0) == 0xE0)
        cont = 2;
    else if ((b & 0xF8) == 0xF0)
        cont = 3;
    for (int i = 0; i < cont; i++) {
        int cc = ed_read_key();
        if (cc > 0xff || ((unsigned char)cc & 0xC0) != 0x80)
            break;
        findchar_seq[findchar_len++] = (char)(unsigned char)cc;
    }
    return 1;
}

/* Last find, for ; and , — vim remembers the char and the motion kind
 * (f/F/t/T) across other commands. */
static char findchar_last_seq[8];
static int findchar_last_len = 0;
static int findchar_last_fwd = 1;
static int findchar_last_till = 0;

static int findchar_motion(Buffer *buf, int line, int col, int forward,
                           int till, TextSelection *sel) {
    if (!findchar_read())
        return 0;
    memcpy(findchar_last_seq, findchar_seq, sizeof(findchar_seq));
    findchar_last_len = findchar_len;
    findchar_last_fwd = forward;
    findchar_last_till = till;
    return textobj_to_char(buf, line, col, findchar_seq, findchar_len, forward,
                           till, sel);
}

int kb_textobj_find_char_fwd(Buffer *buf, int line, int col,
                             TextSelection *sel) {
    return findchar_motion(buf, line, col, 1, 0, sel);
}

int kb_textobj_find_char_back(Buffer *buf, int line, int col,
                              TextSelection *sel) {
    return findchar_motion(buf, line, col, 0, 0, sel);
}

int kb_textobj_till_char_fwd(Buffer *buf, int line, int col,
                             TextSelection *sel) {
    return findchar_motion(buf, line, col, 1, 1, sel);
}

int kb_textobj_till_char_back(Buffer *buf, int line, int col,
                              TextSelection *sel) {
    return findchar_motion(buf, line, col, 0, 1, sel);
}

/* ; and , : repeat the last f/F/t/T, same or reversed direction. A
 * till motion that stalls because the target is adjacent retries one
 * codepoint further, so `;` after `tx` hops occurrence to occurrence
 * like Vim. */
static int findchar_repeat(Buffer *buf, int line, int col, int reverse,
                           TextSelection *sel) {
    if (findchar_last_len == 0)
        return 0;
    int fwd = reverse ? !findchar_last_fwd : findchar_last_fwd;
    if (textobj_to_char(buf, line, col, findchar_last_seq, findchar_last_len,
                        fwd, findchar_last_till, sel))
        return 1;
    if (!findchar_last_till)
        return 0;
    Row *row =
        (buf && line >= 0 && line < buf->num_rows) ? &buf->rows[line] : NULL;
    if (!row)
        return 0;
    int len = (int)row->chars.len;
    int retry;
    if (fwd) {
        if (col >= len)
            return 0;
        int adv = 1;
        utf8_char_width(row->chars.data + col, (size_t)(len - col), &adv);
        if (adv < 1)
            adv = 1;
        retry = col + adv;
    } else {
        if (col <= 0)
            return 0;
        retry = col - 1;
        while (retry > 0 &&
               ((unsigned char)row->chars.data[retry] & 0xC0) == 0x80)
            retry--;
    }
    return textobj_to_char(buf, line, retry, findchar_last_seq,
                           findchar_last_len, fwd, findchar_last_till, sel);
}

int kb_textobj_find_repeat(Buffer *buf, int line, int col, TextSelection *sel) {
    return findchar_repeat(buf, line, col, 0, sel);
}

int kb_textobj_find_repeat_rev(Buffer *buf, int line, int col,
                               TextSelection *sel) {
    return findchar_repeat(buf, line, col, 1, sel);
}

/* Apply a text-object motion to the active window's cursor. Used by
 * the modeless keymap plugins (emacs, vscode) so they don't have to
 * touch cursor.x/cursor.y directly. */
typedef int (*TextObjMotion)(Buffer *, int, int, TextSelection *);

static void kb_apply_motion(TextObjMotion fn) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!buf || !win || !fn)
        return;
    TextSelection sel;
    if (!fn(buf, win->cursor.y, win->cursor.x, &sel))
        return;
    win->cursor.y = sel.cursor.line;
    win->cursor.x = sel.cursor.col;
}

/* Implementation below — count-aware line jump used by gg/G. */
static void goto_line_or(int fallback_y);

void kb_goto_line_start(void) { kb_apply_motion(textobj_to_line_start); }
void kb_goto_line_end(void) { kb_apply_motion(textobj_to_line_end); }
/* kb_goto_file_end: with no count → end of file, with count → that line. */
void kb_goto_file_end(void) {
    Buffer *buf = buf_cur();
    if (!buf)
        return;
    goto_line_or(buf->num_rows - 1);
}
void kb_goto_word_start(void) { kb_apply_motion(textobj_to_word_start); }
void kb_goto_word_end(void) { kb_apply_motion(textobj_to_word_end); }

/* Selection-aware motion helpers (VSCode / modern Emacs semantics):
 *   kb_drop_*   — drop any active selection, then move.
 *   kb_extend_* — enter visual if not already, then move (extending sel).
 * Used by modeless keymaps (vscode_keybinds, emacs_keybinds) to bind
 * Shift+arrow to extend and plain arrow to drop. */

int kb_in_visual(void) {
    return E.mode == MODE_VISUAL || E.mode == MODE_VISUAL_LINE ||
           E.mode == MODE_VISUAL_BLOCK;
}

#define DROP_THEN(name, body)                                                  \
    void kb_drop_##name(void) {                                                \
        if (kb_in_visual())                                                    \
            kb_visual_escape();                                                \
        body;                                                                  \
    }
#define EXTEND_THEN(name, body)                                                \
    void kb_extend_##name(void) {                                              \
        if (!kb_in_visual())                                                   \
            kb_visual_begin(0);                                                \
        body;                                                                  \
    }

DROP_THEN(left, kb_move_left())
DROP_THEN(right, kb_move_right())
DROP_THEN(up, kb_move_up())
DROP_THEN(down, kb_move_down())
DROP_THEN(word_l, kb_goto_word_start())
DROP_THEN(word_r, kb_goto_word_end())
DROP_THEN(bol, kb_goto_line_start())
DROP_THEN(eol, kb_goto_line_end())
DROP_THEN(file_start, kb_goto_file_start())
DROP_THEN(file_end, kb_goto_file_end())
DROP_THEN(page_up, buf_scroll_page_up())
DROP_THEN(page_down, buf_scroll_page_down())

EXTEND_THEN(left, kb_move_left())
EXTEND_THEN(right, kb_move_right())
EXTEND_THEN(up, kb_move_up())
EXTEND_THEN(down, kb_move_down())
EXTEND_THEN(word_l, kb_goto_word_start())
EXTEND_THEN(word_r, kb_goto_word_end())
EXTEND_THEN(bol, kb_goto_line_start())
EXTEND_THEN(eol, kb_goto_line_end())
EXTEND_THEN(file_start, kb_goto_file_start())
EXTEND_THEN(file_end, kb_goto_file_end())
EXTEND_THEN(page_up, buf_scroll_page_up())
EXTEND_THEN(page_down, buf_scroll_page_down())

/* The modeless-keymap basics shared verbatim by the emacs and vscode
 * keymaps: insert-mode Esc/Enter/Tab/Backspace, plain arrows drop the
 * selection, Shift+arrows extend it, Ctrl+Shift+arrows extend word-
 * wise, Shift+Home/End extend to bol/eol. Keymap plugins call this
 * once, then add their own flavor on top (last-write-wins). */
void keybind_register_modeless_basics(void) {
    mapi("<Esc>", kb_insert_escape, "exit insert (no-op when modeless)");
    mapi("<CR>", kb_insert_newline, "newline");
    mapi("<Tab>", kb_insert_tab, "insert tab");
    mapi("<BS>", kb_insert_backspace, "backspace");
    mapv("<Esc>", kb_visual_escape, "exit visual");

    /* Plain motion: drops any active selection. */
    mapi("<Up>", kb_drop_up, "up");
    mapi("<Down>", kb_drop_down, "down");
    mapi("<Left>", kb_drop_left, "left");
    mapi("<Right>", kb_drop_right, "right");
    mapv("<Up>", kb_drop_up, "up");
    mapv("<Down>", kb_drop_down, "down");
    mapv("<Left>", kb_drop_left, "left");
    mapv("<Right>", kb_drop_right, "right");

    /* Shift+motion: enter / extend a selection. */
    mapi("<S-Up>", kb_extend_up, "select up");
    mapi("<S-Down>", kb_extend_down, "select down");
    mapi("<S-Left>", kb_extend_left, "select left");
    mapi("<S-Right>", kb_extend_right, "select right");
    mapv("<S-Up>", kb_extend_up, "extend up");
    mapv("<S-Down>", kb_extend_down, "extend down");
    mapv("<S-Left>", kb_extend_left, "extend left");
    mapv("<S-Right>", kb_extend_right, "extend right");
    mapi("<C-S-Left>", kb_extend_word_l, "select previous word");
    mapi("<C-S-Right>", kb_extend_word_r, "select next word");
    mapv("<C-S-Left>", kb_extend_word_l, "extend previous word");
    mapv("<C-S-Right>", kb_extend_word_r, "extend next word");
    mapi("<S-Home>", kb_extend_bol, "select to bol");
    mapi("<S-End>", kb_extend_eol, "select to eol");
    mapv("<S-Home>", kb_extend_bol, "extend to bol");
    mapv("<S-End>", kb_extend_eol, "extend to eol");
}
/* kb_goto_file_start is defined further below (jump-list aware version
 * used by vim's gg). Both keymap plugins reach it through this header. */

/* gg / G semantics: with no count → file start/end. With a count >= 1
 * → jump to that line (matches vim's `42G` / `42gg`). Consumes the
 * count via keybind_get_and_clear_pending_count so the dispatch loop's
 * repeat-on-count breaks after one call. */
static void goto_line_or(int fallback_y) {
    BUFWIN(buf, win)
    int had_count = keybind_has_pending_count();
    int count = keybind_get_and_clear_pending_count();
    int target;
    if (had_count) {
        target = count - 1;
    } else {
        target = fallback_y;
    }
    if (target < 0)
        target = 0;
    if (target >= buf->num_rows)
        target = buf->num_rows - 1;
    if (abs(target - win->cursor.y) >= 5)
        kb_jump_save_current();
    win->cursor.y = target;
    win->cursor.x = 0;
    buf->cursor->y = target;
    buf->cursor->x = 0;
}

void kb_goto_file_start(void) { goto_line_or(0); }

/* Replace char under cursor with c (stay in normal mode). The
 * interactive read lives in cmd_replace_char (:replace_char). */
void kb_replace_char_apply(int c) {
    ASSERT_EDIT(buf, win)
    if (buf->num_rows == 0)
        return;
    Row *row = &buf->rows[win->cursor.y];
    if (win->cursor.x >= (int)row->chars.len)
        return;

    row->chars.data[win->cursor.x] = (char)c;
    buf_row_update(row);
    buf->dirty++;
    ed_set_status_message("");
}

/* Replace the codepoints in byte range [xs, xe) of row y with one `rc`
 * each. Multi-byte characters collapse to the single replacement byte;
 * a char only partially inside the range is still replaced whole. */
static void replace_row_span(Buffer *buf, int y, int xs, int xe, char rc) {
    if (y < 0 || y >= buf->num_rows)
        return;
    Row *row = &buf->rows[y];
    int len = (int)row->chars.len;
    if (xs < 0)
        xs = 0;
    if (xe > len)
        xe = len;
    if (xs >= xe)
        return;
    undo_record_replace(buf, y);
    StrBuf nb = strbuf_new();
    strbuf_append(&nb, row->chars.data, (size_t)xs);
    int i = xs;
    while (i < xe) {
        int adv = 1;
        utf8_char_width(row->chars.data + i, (size_t)(len - i), &adv);
        if (adv < 1)
            adv = 1;
        strbuf_append_char(&nb, rc);
        i += adv;
    }
    /* i is the first codepoint boundary at or past xe — starting the
     * suffix there keeps a partially-covered multi-byte char whole. */
    strbuf_append(&nb, row->chars.data + i, (size_t)(len - i));
    strbuf_free(&row->chars);
    row->chars = nb;
    buf_row_update(row);
    buf->dirty++;
}

/* Visual-mode r: replace every character of the selection with c
 * (char-wise, line-wise and block-wise). Line breaks are preserved;
 * the cursor lands on the start of the selection. */
void kb_visual_replace_char_apply(int c) {
    ASSERT_EDIT(buf, win)
    if (buf->num_rows == 0)
        return;

    if (c != '\t' && (c < 32 || c > 126)) {
        ed_set_status_message("r: not a printable character");
        return;
    }

    int cy = win->cursor.y, cx = win->cursor.x;
    undo_begin(buf, "visual replace");
    if (win->sel.type == SEL_VISUAL) {
        int sy, sx, ey, ex_excl;
        if (visual_char_range(buf, win, &sy, &sx, &ey, &ex_excl)) {
            for (int y = sy; y <= ey; y++) {
                int xs = (y == sy) ? sx : 0;
                int xe = (y == ey) ? ex_excl : (int)buf->rows[y].chars.len;
                replace_row_span(buf, y, xs, xe, (char)c);
            }
            cy = sy;
            cx = sx;
        }
    } else if (win->sel.type == SEL_VISUAL_LINE) {
        int sy, ey;
        if (visual_line_range(buf, win, &sy, &ey)) {
            for (int y = sy; y <= ey; y++)
                replace_row_span(buf, y, 0, (int)buf->rows[y].chars.len,
                                 (char)c);
            cy = sy;
            cx = 0;
        }
    } else if (win->sel.type == SEL_VISUAL_BLOCK) {
        int sy, ey, srx, erx_excl;
        if (visual_block_range(buf, win, &sy, &ey, &srx, &erx_excl)) {
            for (int y = sy; y <= ey; y++) {
                Row *row = &buf->rows[y];
                replace_row_span(buf, y, buf_row_rx_to_cx(row, srx),
                                 buf_row_rx_to_cx(row, erx_excl), (char)c);
            }
            cy = sy;
            cx = buf_row_rx_to_cx(&buf->rows[sy], srx);
        }
    }
    undo_end(buf);

    win->cursor.y = cy;
    win->cursor.x = cx;
    visual_clear(win);
    ed_set_mode(MODE_NORMAL);
    ed_set_status_message("");
}

void kb_del_win(char direction);
void kb_del_up(void) { kb_del_win('k'); }
void kb_del_down(void) { kb_del_win('j'); }
void kb_del_left(void) { kb_del_win('h'); }
void kb_del_right(void) { kb_del_win('l'); }

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
