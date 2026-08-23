/* vim_keybinds plugin: ships the default Vim-style keymap.
 *
 * All bindings registered here can be overridden later in config_init()
 * (src/config.c) — registrations use last-write-wins, see remove_duplicate
 * in src/keybinds.c. Most bindings cmap onto :commands (cmd_edit.c),
 * so :keybinds shows what each key runs and every action is callable
 * from the : prompt. */

#include "hed.h"

static void register_text_objects(void) {
    /* Basic movement (hjkl + arrows) */
    textobj_register("h", textobj_char_left, "char left");
    textobj_register("j", textobj_line_down, "line down");
    textobj_register("k", textobj_line_up, "line up");
    textobj_register("l", textobj_char_right, "char right");

    /* Single-char motions */
    textobj_register("w", textobj_to_word_end, "word forward");
    textobj_register("b", textobj_to_word_start, "word backward");
    textobj_register("f", kb_textobj_find_char_fwd, "to char");
    textobj_register("F", kb_textobj_find_char_back, "to char backward");
    textobj_register("t", kb_textobj_till_char_fwd, "till char");
    textobj_register("T", kb_textobj_till_char_back, "till char backward");
    textobj_register(";", kb_textobj_find_repeat, "repeat find");
    textobj_register(",", kb_textobj_find_repeat_rev, "repeat find reversed");
    textobj_register("e", textobj_to_word_end, "word end");
    textobj_register("W", textobj_to_WORD_end, "WORD forward");
    textobj_register("B", textobj_to_WORD_start, "WORD backward");
    textobj_register("}", textobj_to_paragraph_end, "next paragraph");
    textobj_register("{", textobj_to_paragraph_start, "prev paragraph");
    textobj_register("$", textobj_to_line_end, "end of line");
    textobj_register("0", textobj_to_line_start, "beginning of line");
    textobj_register("G", textobj_to_file_end, "end of file");
    textobj_register("gg", textobj_to_file_start, "start of file");

    /* Inner objects */
    textobj_register("iw", textobj_word, "inner word");
    textobj_register("iW", textobj_WORD, "inner WORD");
    textobj_register("ip", textobj_paragraph, "inner paragraph");
    textobj_register("i(", textobj_brackets, "inner parentheses");
    textobj_register("i)", textobj_brackets, "inner parentheses");
    textobj_register("ib", textobj_brackets, "inner brackets");

    /* Around objects */
    textobj_register("aw", textobj_word_around, "around word");
    textobj_register("aW", textobj_WORD_around, "around WORD");
    textobj_register("ap", textobj_paragraph, "around paragraph");
}

static int vim_keybinds_init(void) {
    register_text_objects();

    /* --- Insert mode --- */
    mapi("<Esc>", kb_insert_escape, "exit insert");
    mapi("<CR>", kb_insert_newline, "new line");
    mapi("<Tab>", kb_insert_tab, "insert tab");
    mapi("<BS>", kb_insert_backspace, "backspace");
    mapi("<C-h>", kb_insert_backspace, "backspace");
    mapi("<Up>", kb_move_up, "move up");
    mapi("<Down>", kb_move_down, "move down");
    mapi("<Left>", kb_move_left, "move left");
    mapi("<Right>", kb_move_right, "move right");
    cmapn("/", "search", "search");

    cmapn("O", "new_line_above", "new line above");
    cmapn("o", "new_line", "new line below");
    cmapn("U", "redo", "redo");
    cmapn("u", "undo", "undo");
    cmapn(".", "repeat", "repeat last edit");
    cmapn("q", "record", "record macro");
    cmapn("@", "play", "play macro");
    cmapn("ZQ", "q!", "quit (force)");
    cmapn("ZZ", "wq", "save and quit");
    cmapn("%", "match_bracket", "match bracket");
    cmapv("%", "match_bracket", "match bracket");
    cmapn("*", "search_word", "find word");
    cmapv("*", "search_selection", "find selection (exits visual)");
    cmapn("<C-*>", "search_word", "find word");

    /* --- Visual mode --- */
    mapv("h", kb_move_left, "left");
    mapv("j", kb_move_down, "down");
    mapv("k", kb_move_up, "up");
    mapv("l", kb_move_right, "right");
    mapv("<Left>", kb_move_left, "left");
    mapv("<Down>", kb_move_down, "down");
    mapv("<Up>", kb_move_up, "up");
    mapv("<Right>", kb_move_right, "right");
    /* VL/VB dispatch falls through to MODE_VISUAL bindings, so one
     * cmapv covers all three visual modes. */
    cmapv("y", "yank", "yank");
    cmapv("d", "delete", "delete");
    cmapv("x", "delete", "delete selection");
    cmapv("r", "replace_char", "replace selection chars");
    cmapv("p", "put", "paste over selection");
    cmapv("P", "put!", "paste over selection");
    cmapv("v", "visual", "exit visual");
    cmapv("<C-v>", "visual_block", "block mode");
    cmapv("<Esc>", "visual", "exit visual");
    cmapv("i", "insert", "insert");
    cmapv("a", "append", "append");
    /* Leave win->sel intact across the : prompt so commands like
     * :shell foo >%v can act on it. ed_set_mode defers the visual
     * clear until MODE_COMMAND itself exits. */
    mapv(":", kb_enter_command_mode, "command");
    mapn(":", kb_enter_command_mode, "command");
    cmapn("V", "visual_line", "visual line");
    cmapv("V", "visual_line", "switch to / exit visual line");
    cmapn("<<", "unindent", "unindent");
    cmapn("<C-d>", "scrolldown", "scroll down");
    cmapn("<C-v>", "visual_block", "visual block");
    cmapn("}", "goto }", "next paragraph");
    cmapn("{", "goto {", "prev paragraph");
    cmapn(" jf", "jump_forward", "jump forward");
    cmapn(" jb", "jump_back", "jump back");
    cmapn("<Tab>", "jump_forward", "jump forward");
    cmapn("<C-o>", "jump_back", "jump back");
    cmapn("<C-u>", "scrollup", "scroll up");
    cmapn(">>", "indent", "indent");
    cmapn("<Right>", "wgrowwidth", "grow window width");
    cmapn("<Left>", "wshrinkwidth", "shrink window width");
    cmapn("<Down>", "wgrowheight", "grow window height");
    cmapn("<Up>", "wshrinkheight", "shrink window height");
    cmapn("A", "append_eol", "append eol");
    cmapn("I", "insert_bol", "insert bol");
    cmapn("J", "join", "join lines");
    cmapn("a", "append", "append");
    cmapn("<C-r>", "redo", "redo");

    /* Operator keybindings - read a text object, or take it as an
     * argument from the : prompt (":delete iw"). */
    cmapn("d", "delete", "delete operator");
    cmapn("c", "change", "change operator");
    cmapn("y", "yank", "yank operator");
    cmapn("v", "select", "visual select with motion");
    cmapn("dd", "delete_line", "del line");
    mapn("gg", kb_goto_file_start, "start of file (or line N with count)");
    mapn("G", kb_goto_file_end, "end of file (or line N with count)");
    cmapn("gc", "toggle_comment", "toggle comment");
    cmapn("gf", "openpath", "open file");
    cmapn("gF", "searchpath", "search file");
    cmapn("i", "insert", "insert");
    cmapn("n", "search_next", "next match");
    cmapn("p", "put", "paste after");
    cmapn("P", "put!", "paste before");
    cmapn("r", "replace_char", "replace char");
    cmapn("x", "delete_char", "del char");
    cmapn("yy", "yank_line", "yank line");
    cmapn("za", "foldtoggle", "fold toggle");
    cmapn("zc", "foldclose", "fold close");
    cmapn("zM", "foldcloseall", "close all folds");
    cmapn("zo", "foldopen", "fold open");
    cmapn("zR", "foldopenall", "open all folds");
    cmapn("<S-Tab>", "foldcycle", "cycle fold level 1/2/all/none");
    cmapn("zz", "center", "center screen");
    cmapn("~", "toggle_case", "toggle case");
    cmapn("D", "delete_eol", "del to line end");
    cmapn("C", "change_eol", "change to line end");
    cmapn("S", "change_line", "change line");
    return 0;
}

const Plugin plugin_vim_keybinds = {
    .name = "vim_keybinds",
    .desc = "default Vim-style modal keymap",
    .init = vim_keybinds_init,
    .deinit = NULL,
};
