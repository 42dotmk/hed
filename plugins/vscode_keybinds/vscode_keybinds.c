#include "vscode_keybinds.h"
#include "hed.h"

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* Ctrl+A: select the whole buffer. */
static void vsc_select_all(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!buf || !win || buf->num_rows == 0)
        return;
    if (kb_in_visual())
        kb_visual_escape();
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

/* Del: delete forward (the selection if one is active; joins with the
 * next line at eol). */
static void vsc_delete_forward(void) {
    if (kb_in_visual()) {
        kb_visual_delete_selection();
        return;
    }
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!buf || !win || buf->num_rows == 0)
        return;
    Row *row = &buf->rows[win->cursor.y];
    if (win->cursor.x >= (int)row->chars.len) {
        if (win->cursor.y >= buf->num_rows - 1)
            return;
        win->cursor.y++;
        win->cursor.x = 0;
        kb_insert_backspace(); /* join with the line we came from */
    } else {
        cmd_delete_char(NULL);
    }
}

/* Scan one word (or whitespace-then-punctuation run) from `x` in
 * direction `dir` (-1 left, +1 right) and return the far boundary.
 * VSCode semantics: skip adjacent whitespace first, then consume one
 * run of word bytes or one run of punctuation. */
static int vsc_word_boundary(const Row *row, int x, int dir) {
    const char *s = row->chars.data;
    int len = (int)row->chars.len;
    int i = x;
#define AT(j) ((unsigned char)s[(dir) < 0 ? (j) - 1 : (j)])
#define MORE(j) ((dir) < 0 ? (j) > 0 : (j) < len)
    while (MORE(i) && isspace(AT(i)))
        i += dir;
    if (MORE(i)) {
        if (textobj_is_word_byte(AT(i))) {
            while (MORE(i) && textobj_is_word_byte(AT(i)))
                i += dir;
        } else {
            while (MORE(i) && !textobj_is_word_byte(AT(i)) && !isspace(AT(i)))
                i += dir;
        }
    }
#undef AT
#undef MORE
    return i;
}

/* Delete [from, to) on the cursor row as one undo step. */
static void vsc_delete_span(int from, int to) {
    Window *win = window_cur();
    if (!win || from >= to)
        return;
    int y = win->cursor.y;
    TextSelection sel = {.start = {y, from},
                         .end = {y, to},
                         .cursor = {y, from},
                         .type = SEL_VISUAL};
    buf_delete_selection(&sel);
}

/* Ctrl+Backspace: delete the word (or whitespace/punctuation run) left
 * of the cursor. At column 0 joins with the previous line. */
static void vsc_delete_word_left(void) {
    if (kb_in_visual()) {
        kb_visual_delete_selection();
        return;
    }
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!buf || !win || buf->num_rows == 0)
        return;
    if (win->cursor.x == 0) {
        kb_insert_backspace();
        return;
    }
    Row *row = &buf->rows[win->cursor.y];
    vsc_delete_span(vsc_word_boundary(row, win->cursor.x, -1), win->cursor.x);
}

/* Ctrl+Del: delete the word right of the cursor. At eol joins lines. */
static void vsc_delete_word_right(void) {
    if (kb_in_visual()) {
        kb_visual_delete_selection();
        return;
    }
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!buf || !win || buf->num_rows == 0)
        return;
    Row *row = &buf->rows[win->cursor.y];
    if (win->cursor.x >= (int)row->chars.len) {
        vsc_delete_forward();
        return;
    }
    vsc_delete_span(win->cursor.x, vsc_word_boundary(row, win->cursor.x, +1));
}

/* ------------------------------------------------------------------ */
/* Keymap                                                              */
/* ------------------------------------------------------------------ */

static int vscode_keybinds_init(void) {
    /* Esc/CR/Tab/BS + arrow drop/extend selection (shared with the
     * emacs keymap). */
    keybind_register_modeless_basics();

    mapi("<Del>", vsc_delete_forward, "delete forward");
    cmapv("<Del>", "delete", "delete selection");
    cmapv("<BS>", "delete", "delete selection");
    mapi("<C-h>", vsc_delete_word_left, "delete word left (Ctrl+Backspace)");
    mapi("<C-Del>", vsc_delete_word_right, "delete word right");

    /* Whole-buffer / line selection. */
    mapi("<C-a>", vsc_select_all, "select all");
    mapv("<C-a>", vsc_select_all, "select all");
    mapi("<C-l>", vsc_select_line, "select line");
    mapv("<C-l>", vsc_select_line, "extend selection by a line");

    /* File-edge motion (Ctrl+Home/End) and selection (+Shift). */
    mapi("<C-Home>", kb_drop_file_start, "start of file");
    mapi("<C-End>", kb_drop_file_end, "end of file");
    mapv("<C-Home>", kb_drop_file_start, "start of file");
    mapv("<C-End>", kb_drop_file_end, "end of file");
    mapi("<C-S-Home>", kb_extend_file_start, "select to start of file");
    mapi("<C-S-End>", kb_extend_file_end, "select to end of file");
    mapv("<C-S-Home>", kb_extend_file_start, "extend to start of file");
    mapv("<C-S-End>", kb_extend_file_end, "extend to end of file");

    /* Paging. */
    mapi("<PageUp>", kb_drop_page_up, "page up");
    mapi("<PageDown>", kb_drop_page_down, "page down");
    mapv("<PageUp>", kb_drop_page_up, "page up");
    mapv("<PageDown>", kb_drop_page_down, "page down");
    mapi("<S-PageUp>", kb_extend_page_up, "select page up");
    mapi("<S-PageDown>", kb_extend_page_down, "select page down");
    mapv("<S-PageUp>", kb_extend_page_up, "extend page up");
    mapv("<S-PageDown>", kb_extend_page_down, "extend page down");

    /* File / window / buffer. */
    cmapi("<C-s>", "w", "save");
    cmapi("<C-n>", "new", "new buffer");
    cmapi("<C-o>", "fzf", "open file");
    cmapi("<C-p>", "fzf", "quick open");
    cmapi("<C-e>", "recent", "recent files");
    cmapi("<C-w>", "wclose", "close window");
    cmapi("<C-\\>", "vsplit", "split editor");
    cmapi("<M-\\>", "vsplit", "split vertical");
    cmapi("<M-->", "split", "split horizontal");
    cmapi("<M-n>", "bn", "next buffer");
    cmapi("<M-N>", "bp", "prev buffer");
    cmapi("<C-PageDown>", "bn", "next buffer (Ctrl+PgDn)");
    cmapi("<C-PageUp>", "bp", "prev buffer (Ctrl+PgUp)");

    /* Command palette. F1 / Alt+P because terminals can't deliver
     * Ctrl+Shift+P. */
    cmapi("<M-p>", "prompt", "command palette");
    cmapi("<F1>", "prompt", "command palette");

    /* Undo / redo / clipboard. */
    cmapi("<C-z>", "undo", "undo");
    cmapi("<C-y>", "redo", "redo");
    cmapi("<C-v>", "put", "paste");
    /* No selection: line-wise copy/cut (VSCode semantics). The cmapn
     * duplicates keep these working if modeless is toggled off. */
    cmapi("<C-c>", "yank_line", "copy line");
    cmapi("<C-x>", "delete_line", "cut line");
    cmapn("<C-c>", "yank_line", "copy line");
    cmapn("<C-x>", "delete_line", "cut line");
    cmapv("<C-c>", "yank", "copy selection");
    cmapv("<C-x>", "delete", "cut selection");

    /* Find & navigate. */
    cmapi("<C-f>", "search", "find in file");
    cmapi("<C-S-f>", "rg", "search in workspace");
    cmapi("<F3>", "search_next", "find next");
    cmapi("<C-g>", "prompt goto", "go to line");
    cmapi("<M-Left>", "jump_back", "navigate back");
    cmapi("<M-Right>", "jump_forward", "navigate forward");
    cmapi("<F12>", "tag", "go to definition (ctags)");
    cmapi("<C-t>", "tag", "go to symbol (ctags)");

    /* Multi-cursor (VSCode Ctrl+D family, via the multicursor plugin). */
    cmapi("<C-d>", "mc_next_match", "add cursor at next occurrence");
    cmapv("<C-d>", "mc_next_match", "add cursor at next match of selection");
    cmapi("<C-k><C-d>", "mc_skip", "skip occurrence, take next");
    cmapi("<M-C-Up>", "mc_add_above", "add cursor above");
    cmapi("<M-C-Down>", "mc_add_below", "add cursor below");
    cmapi("<C-k><C-s>", "mc_sync toggle", "toggle synced multi-cursor edits");
    cmapi("<C-k><Esc>", "mc_clear", "clear extra cursors");

    /* Line operations. */
    cmapi("<M-Up>", "move_line_up", "move line up");
    cmapi("<M-Down>", "move_line_down", "move line down");
    cmapi("<M-S-Down>", "duplicate_line", "duplicate line");
    cmapi("<C-]>", "indent", "indent line");
    cmapi("<S-Tab>", "unindent", "unindent line");
    cmapv("<C-]>", "indent", "indent line");
    cmapv("<S-Tab>", "unindent", "unindent line");
    cmapi("<C-_>", "toggle_comment", "toggle comment (Ctrl+/)");
    cmapv("<C-_>", "toggle_comment", "toggle comment (Ctrl+/)");
    cmapi("<M-/>", "toggle_comment", "toggle comment");
    cmapi("<M-F>", "fmt", "format document (Shift+Alt+F)");

    /* Folding (VSCode Ctrl+K chords; Ctrl+0 isn't deliverable, so the
     * fold-all chord is Ctrl+K then plain 0). */
    cmapi("<C-k><C-l>", "foldtoggle", "toggle fold");
    cmapi("<C-k><C-j>", "foldopenall", "unfold all");
    cmapi("<C-k>0", "foldcloseall", "fold all");

    /* Word motion. */
    mapi("<Home>", kb_drop_bol, "beginning of line");
    mapi("<End>", kb_drop_eol, "end of line");
    mapi("<C-Left>", kb_drop_word_l, "previous word");
    mapi("<C-Right>", kb_drop_word_r, "next word");
    mapv("<C-Left>", kb_drop_word_l, "previous word");
    mapv("<C-Right>", kb_drop_word_r, "next word");

    ed_set_modeless(1);
    return 0;
}

const Plugin plugin_vscode_keybinds = {
    .name = "vscode_keybinds",
    .desc = "VSCode-flavored keymap (modeless, Ctrl-key oriented)",
    .init = vscode_keybinds_init,
    .deinit = NULL,
};
