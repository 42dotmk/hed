#include "vscode_keybinds.h"
#include "hed.h"
#include "input/command_mode.h"
#include "input/prompt.h"

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static int vsc_in_visual(void) {
    return E.mode == MODE_VISUAL || E.mode == MODE_VISUAL_LINE ||
           E.mode == MODE_VISUAL_BLOCK;
}

/* Word-ish byte for Ctrl+Backspace / Ctrl+Del word deletion. Bytes
 * >= 0x80 (UTF-8 continuations and starts) count as word bytes so a
 * multi-byte char is never split across a boundary decision. */
static int vsc_word_byte(unsigned char c) {
    return isalnum(c) || c == '_' || c >= 0x80;
}

/* Ctrl+G: open the ":" prompt prefilled with "goto " so typing a
 * number + Enter jumps to it (VSCode's Go to Line). */
static void vsc_goto_line_prompt(void) {
    cmd_prompt_open();
    Prompt *p = prompt_current();
    if (p) prompt_set_text(p, "goto ", 5);
}

/* Ctrl+A: select the whole buffer. */
static void vsc_select_all(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!buf || !win || buf->num_rows == 0) return;
    if (vsc_in_visual()) kb_visual_escape();
    win->cursor.y = 0;
    win->cursor.x = 0;
    kb_visual_begin(0);
    kb_goto_file_end();
    kb_goto_line_end();
}

/* Ctrl+L: select the current line; repeat to extend a line at a time. */
static void vsc_select_line(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!buf || !win || buf->num_rows == 0) return;
    if (!vsc_in_visual()) {
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

/* Ctrl+Home / Ctrl+End: jump to file start/end, dropping any selection. */
static void vsc_goto_file_start(void) {
    if (vsc_in_visual()) kb_visual_escape();
    kb_goto_file_start();
}
static void vsc_goto_file_end(void) {
    if (vsc_in_visual()) kb_visual_escape();
    kb_goto_file_end();
}

/* Ctrl+Shift+Home / Ctrl+Shift+End: extend selection to file edges. */
static void vsc_extend_file_start(void) {
    if (!vsc_in_visual()) kb_visual_begin(0);
    kb_goto_file_start();
}
static void vsc_extend_file_end(void) {
    if (!vsc_in_visual()) kb_visual_begin(0);
    kb_goto_file_end();
}

/* PageUp / PageDown (plain drops selection, shifted extends it). */
static void vsc_page_up(void) {
    if (vsc_in_visual()) kb_visual_escape();
    buf_scroll_page_up();
}
static void vsc_page_down(void) {
    if (vsc_in_visual()) kb_visual_escape();
    buf_scroll_page_down();
}
static void vsc_extend_page_up(void) {
    if (!vsc_in_visual()) kb_visual_begin(0);
    buf_scroll_page_up();
}
static void vsc_extend_page_down(void) {
    if (!vsc_in_visual()) kb_visual_begin(0);
    buf_scroll_page_down();
}

/* Del: delete forward (the selection if one is active; joins with the
 * next line at eol). */
static void vsc_delete_forward(void) {
    if (vsc_in_visual()) {
        kb_visual_delete_selection();
        return;
    }
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!buf || !win || buf->num_rows == 0) return;
    Row *row = &buf->rows[win->cursor.y];
    if (win->cursor.x >= (int)row->chars.len) {
        if (win->cursor.y >= buf->num_rows - 1) return;
        win->cursor.y++;
        win->cursor.x = 0;
        kb_insert_backspace(); /* join with the line we came from */
    } else {
        kb_delete_char();
    }
}

/* Ctrl+Backspace: delete the word (or whitespace/punctuation run) left
 * of the cursor. At column 0 joins with the previous line. */
static void vsc_delete_word_left(void) {
    if (vsc_in_visual()) {
        kb_visual_delete_selection();
        return;
    }
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!buf || !win || buf->num_rows == 0) return;
    if (win->cursor.x == 0) {
        kb_insert_backspace();
        return;
    }
    Row *row = &buf->rows[win->cursor.y];
    const char *s = row->chars.data;
    int x = win->cursor.x;
    while (x > 0 && isspace((unsigned char)s[x - 1])) x--;
    if (x > 0) {
        if (vsc_word_byte((unsigned char)s[x - 1])) {
            while (x > 0 && vsc_word_byte((unsigned char)s[x - 1])) x--;
        } else {
            while (x > 0 && !vsc_word_byte((unsigned char)s[x - 1]) &&
                   !isspace((unsigned char)s[x - 1]))
                x--;
        }
    }
    int prev = win->cursor.x;
    while (win->cursor.x > x) {
        kb_insert_backspace();
        if (win->cursor.x == prev) break; /* read-only buffer etc. */
        prev = win->cursor.x;
    }
}

/* Ctrl+Del: delete the word right of the cursor. At eol joins lines. */
static void vsc_delete_word_right(void) {
    if (vsc_in_visual()) {
        kb_visual_delete_selection();
        return;
    }
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!buf || !win || buf->num_rows == 0) return;
    Row *row = &buf->rows[win->cursor.y];
    int len = (int)row->chars.len;
    int x = win->cursor.x;
    if (x >= len) {
        vsc_delete_forward();
        return;
    }
    const char *s = row->chars.data;
    int e = x;
    while (e < len && isspace((unsigned char)s[e])) e++;
    if (e < len) {
        if (vsc_word_byte((unsigned char)s[e])) {
            while (e < len && vsc_word_byte((unsigned char)s[e])) e++;
        } else {
            while (e < len && !vsc_word_byte((unsigned char)s[e]) &&
                   !isspace((unsigned char)s[e]))
                e++;
        }
    }
    int ndel = e - x;
    while (ndel > 0) {
        int before = (int)row->chars.len;
        kb_delete_char();
        int removed = before - (int)row->chars.len;
        if (removed <= 0) break; /* read-only buffer etc. */
        ndel -= removed;
    }
}

/* ------------------------------------------------------------------ */
/* Keymap                                                              */
/* ------------------------------------------------------------------ */

static int vscode_keybinds_init(void) {
    /* Universal insert-mode keys (don't rely on vim_keybinds). */
    mapi("<Esc>",   kb_insert_escape,    "exit insert (no-op when modeless)");
    mapi("<CR>",    kb_insert_newline,   "newline");
    mapi("<Tab>",   kb_insert_tab,       "insert tab");
    mapi("<BS>",    kb_insert_backspace, "backspace");
    mapv("<Esc>",   kb_visual_escape,    "exit visual");
    mapi("<Del>",   vsc_delete_forward,  "delete forward");
    mapv("<Del>",   kb_visual_delete_selection, "delete selection");
    mapv("<BS>",    kb_visual_delete_selection, "delete selection");
    mapi("<C-h>",   vsc_delete_word_left,  "delete word left (Ctrl+Backspace)");
    mapi("<C-Del>", vsc_delete_word_right, "delete word right");

    /* Plain motion: drop any active selection. Bound in both insert and
     * visual so an unmodified arrow exits the selection. */
    mapi("<Up>",    kb_drop_up,    "up");
    mapi("<Down>",  kb_drop_down,  "down");
    mapi("<Left>",  kb_drop_left,  "left");
    mapi("<Right>", kb_drop_right, "right");
    mapv("<Up>",    kb_drop_up,    "up");
    mapv("<Down>",  kb_drop_down,  "down");
    mapv("<Left>",  kb_drop_left,  "left");
    mapv("<Right>", kb_drop_right, "right");

    /* Shift+motion: enter (or extend) a selection. */
    mapi("<S-Up>",     kb_extend_up,    "select up");
    mapi("<S-Down>",   kb_extend_down,  "select down");
    mapi("<S-Left>",   kb_extend_left,  "select left");
    mapi("<S-Right>",  kb_extend_right, "select right");
    mapv("<S-Up>",     kb_extend_up,    "extend up");
    mapv("<S-Down>",   kb_extend_down,  "extend down");
    mapv("<S-Left>",   kb_extend_left,  "extend left");
    mapv("<S-Right>",  kb_extend_right, "extend right");

    /* Ctrl+Shift+arrow: word-wise select. */
    mapi("<C-S-Left>",  kb_extend_word_l, "select previous word");
    mapi("<C-S-Right>", kb_extend_word_r, "select next word");
    mapv("<C-S-Left>",  kb_extend_word_l, "extend previous word");
    mapv("<C-S-Right>", kb_extend_word_r, "extend next word");

    /* Shift+Home/End: select to bol/eol. */
    mapi("<S-Home>", kb_extend_bol, "select to bol");
    mapi("<S-End>",  kb_extend_eol, "select to eol");
    mapv("<S-Home>", kb_extend_bol, "extend to bol");
    mapv("<S-End>",  kb_extend_eol, "extend to eol");

    /* Whole-buffer / line selection. */
    mapi("<C-a>", vsc_select_all,  "select all");
    mapv("<C-a>", vsc_select_all,  "select all");
    mapi("<C-l>", vsc_select_line, "select line");
    mapv("<C-l>", vsc_select_line, "extend selection by a line");

    /* File-edge motion (Ctrl+Home/End) and selection (+Shift). */
    mapi("<C-Home>",   vsc_goto_file_start,   "start of file");
    mapi("<C-End>",    vsc_goto_file_end,     "end of file");
    mapv("<C-Home>",   vsc_goto_file_start,   "start of file");
    mapv("<C-End>",    vsc_goto_file_end,     "end of file");
    mapi("<C-S-Home>", vsc_extend_file_start, "select to start of file");
    mapi("<C-S-End>",  vsc_extend_file_end,   "select to end of file");
    mapv("<C-S-Home>", vsc_extend_file_start, "extend to start of file");
    mapv("<C-S-End>",  vsc_extend_file_end,   "extend to end of file");

    /* Paging. */
    mapi("<PageUp>",     vsc_page_up,          "page up");
    mapi("<PageDown>",   vsc_page_down,        "page down");
    mapv("<PageUp>",     vsc_page_up,          "page up");
    mapv("<PageDown>",   vsc_page_down,        "page down");
    mapi("<S-PageUp>",   vsc_extend_page_up,   "select page up");
    mapi("<S-PageDown>", vsc_extend_page_down, "select page down");
    mapv("<S-PageUp>",   vsc_extend_page_up,   "extend page up");
    mapv("<S-PageDown>", vsc_extend_page_down, "extend page down");

    /* File / window / buffer. */
    cmapi("<C-s>",        "w",      "save");
    cmapi("<C-n>",        "new",    "new buffer");
    cmapi("<C-o>",        "fzf",    "open file");
    cmapi("<C-p>",        "fzf",    "quick open");
    cmapi("<C-e>",        "recent", "recent files");
    cmapi("<C-w>",        "wclose", "close window");
    cmapi("<C-\\>",       "vsplit", "split editor");
    cmapi("<M-\\>",       "vsplit", "split vertical");
    cmapi("<M-->",        "split",  "split horizontal");
    cmapi("<M-n>",        "bn",     "next buffer");
    cmapi("<M-N>",        "bp",     "prev buffer");
    cmapi("<C-PageDown>", "bn",     "next buffer (Ctrl+PgDn)");
    cmapi("<C-PageUp>",   "bp",     "prev buffer (Ctrl+PgUp)");

    /* Command palette. F1 / Alt+P because terminals can't deliver
     * Ctrl+Shift+P. */
    mapi("<M-p>", kb_enter_command_mode, "command palette");
    mapi("<F1>",  kb_enter_command_mode, "command palette");

    /* Undo / redo / clipboard. */
    cmapi("<C-z>", "undo", "undo");
    cmapi("<C-y>", "redo", "redo");
    mapi("<C-v>", kb_paste,                   "paste");
    /* No selection: line-wise copy/cut (VSCode semantics). The mapn
     * duplicates keep these working if modeless is toggled off. */
    mapi("<C-c>", kb_yank_line,               "copy line");
    mapi("<C-x>", kb_delete_line,             "cut line");
    mapn("<C-c>", kb_yank_line,               "copy line");
    mapn("<C-x>", kb_delete_line,             "cut line");
    mapv("<C-c>", kb_visual_yank_selection,   "copy selection");
    mapv("<C-x>", kb_visual_delete_selection, "cut selection");

    /* Find & navigate. */
    mapi("<C-f>",    kb_search_prompt,       "find in file");
    cmapi("<C-S-f>", "rg",                   "search in workspace");
    mapi("<F3>",     kb_search_next,         "find next");
    mapi("<C-g>",    vsc_goto_line_prompt,   "go to line");
    mapi("<M-Left>", kb_jump_backward,       "navigate back");
    mapi("<M-Right>",kb_jump_forward,        "navigate forward");
    cmapi("<F12>",   "tag",                  "go to definition (ctags)");
    cmapi("<C-t>",   "tag",                  "go to symbol (ctags)");

    /* Multi-cursor (VSCode Ctrl+D family, via the multicursor plugin). */
    cmapi("<C-d>",      "mc_next_match", "add cursor at next occurrence");
    cmapv("<C-d>",      "mc_next_match", "add cursor at next match of selection");
    cmapi("<C-k><C-d>", "mc_skip",       "skip occurrence, take next");
    cmapi("<M-C-Up>",   "mc_add_above",  "add cursor above");
    cmapi("<M-C-Down>", "mc_add_below",  "add cursor below");
    cmapi("<C-k><C-s>", "mc_sync toggle","toggle synced multi-cursor edits");
    cmapi("<C-k><Esc>", "mc_clear",      "clear extra cursors");

    /* Line operations. */
    mapi("<M-Up>",     buf_move_line_up,   "move line up");
    mapi("<M-Down>",   buf_move_line_down, "move line down");
    mapi("<M-S-Down>", buf_duplicate_line, "duplicate line");
    mapi("<C-]>",      buf_indent_line,    "indent line");
    mapi("<S-Tab>",    buf_unindent_line,  "unindent line");
    mapv("<C-]>",      buf_indent_line,    "indent line");
    mapv("<S-Tab>",    buf_unindent_line,  "unindent line");
    mapi("<C-_>",      buf_toggle_comment, "toggle comment (Ctrl+/)");
    mapv("<C-_>",      buf_toggle_comment, "toggle comment (Ctrl+/)");
    mapi("<M-/>",      buf_toggle_comment, "toggle comment");
    cmapi("<M-F>",     "fmt",              "format document (Shift+Alt+F)");

    /* Folding (VSCode Ctrl+K chords; Ctrl+0 isn't deliverable, so the
     * fold-all chord is Ctrl+K then plain 0). */
    mapi("<C-k><C-l>", kb_fold_toggle,    "toggle fold");
    mapi("<C-k><C-j>", kb_fold_open_all,  "unfold all");
    mapi("<C-k>0",     kb_fold_close_all, "fold all");

    /* Word motion. */
    mapi("<Home>",    kb_drop_bol,    "beginning of line");
    mapi("<End>",     kb_drop_eol,    "end of line");
    mapi("<C-Left>",  kb_drop_word_l, "previous word");
    mapi("<C-Right>", kb_drop_word_r, "next word");
    mapv("<C-Left>",  kb_drop_word_l, "previous word");
    mapv("<C-Right>", kb_drop_word_r, "next word");

    ed_set_modeless(1);
    return 0;
}

const Plugin plugin_vscode_keybinds = {
    .name   = "vscode_keybinds",
    .desc   = "VSCode-flavored keymap (modeless, Ctrl-key oriented)",
    .init   = vscode_keybinds_init,
    .deinit = NULL,
};
