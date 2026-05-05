/* vim_keybinds plugin: ships the default Vim-style keymap.
 *
 * All bindings registered here can be overridden later in config_init()
 * (src/config.c) — registrations use last-write-wins, see remove_duplicate
 * in src/keybinds.c. */

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
    textobj_register("ip", textobj_paragraph, "inner paragraph");
    textobj_register("i(", textobj_brackets, "inner parentheses");
    textobj_register("i)", textobj_brackets, "inner parentheses");
    textobj_register("ib", textobj_brackets, "inner brackets");

    /* Around objects */
    textobj_register("aw", textobj_word, "around word");
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
    mapn("/", kb_search_prompt, "search");

    cmapn("O", "new_line_above");
    cmapn("o", "new_line");
    cmapn("U", "redo");
    cmapn("u", "undo");
    cmapn(".", "repeat");
    cmapn("q", "record");
    cmapn("@", "play");
    cmapn("ZQ", "q!");
    cmapn("ZZ", "wq");
    mapn("%", buf_find_matching_bracket, "match bracket");
    mapv("%", buf_find_matching_bracket, "match bracket");
    mapn("*", kb_find_under_cursor, "find word");
    mapn("<C-*>", kb_find_under_cursor, "find word");

    /* --- Visual mode --- */
    mapv("h", kb_move_left, "left");
    mapv("j", kb_move_down, "down");
    mapv("k", kb_move_up, "up");
    mapv("l", kb_move_right, "right");
    mapv("<Left>", kb_move_left, "left");
    mapv("<Down>", kb_move_down, "down");
    mapv("<Up>", kb_move_up, "up");
    mapv("<Right>", kb_move_right, "right");
    mapv("y", kb_visual_yank_selection, "yank");
    mapv("d", kb_visual_delete_selection, "delete");
    mapv("x",  kb_visual_delete_selection, "delete selection");
    mapvl("x", kb_visual_delete_selection, "delete selection");
    mapvb("x", kb_visual_delete_selection, "delete selection");
    mapv("v", kb_visual_escape, "exit visual");
    mapv("<C-v>", kb_visual_toggle_block_mode, "block mode");
    mapv("<Esc>", kb_visual_escape, "exit visual");
    mapv("i", kb_visual_enter_insert_mode, "insert");
    mapv("a", kb_visual_enter_append_mode, "append");
    mapv(":", kb_visual_enter_command_mode, "command");
    mapn(":", kb_enter_command_mode, "command");
    mapn("V", kb_visual_line_toggle, "visual line");
    mapv("V", kb_visual_line_toggle, "switch to visual line");
    mapvl("V", kb_visual_escape, "exit visual line");
    mapn("<<", buf_unindent_line, "unindent");
    mapn("<C-d>", buf_scroll_half_page_down, "scroll down");
    mapn("<C-v>", kb_visual_block_toggle, "visual block");
    mapn("}", kb_para_next, "next paragraph");
    mapn("{", kb_para_prev, "prev paragraph");
    mapn(" jf", kb_jump_forward, "jump forward");
    mapn(" jb", kb_jump_backward, "jump back");
    mapn("<Tab>", kb_jump_forward, "jump forward");
    mapn("<C-o>", kb_jump_backward, "jump back");
    mapn("<C-u>", buf_scroll_half_page_up, "scroll up");
    mapn(">>", buf_indent_line, "indent");
    mapn("<Right>", kb_win_grow_width,    "grow window width");
    mapn("<Left>",  kb_win_shrink_width,  "shrink window width");
    mapn("<Down>",  kb_win_grow_height,   "grow window height");
    mapn("<Up>",    kb_win_shrink_height, "shrink window height");
    mapn("A", kb_end_append, "append eol");
    mapn("I", kb_start_insert, "insert bol");
    mapn("J", buf_join_lines, "join lines");
    mapn("a", kb_append_mode, "append");
    cmapn("<C-r>", "redo");

    /* Operator keybindings - wait for text object input */
    mapn("d", kb_operator_delete, "delete operator");
    mapn("c", kb_operator_change, "change operator");
    mapn("y", kb_operator_yank, "yank operator");
    mapn("v", kb_operator_select, "visual select with motion");
    mapn("dd", kb_delete_line, "del line");
    mapn("gg", kb_goto_file_start, "start of file (or line N with count)");
    mapn("G",  kb_goto_file_end,   "end of file (or line N with count)");
    mapn("gc", buf_toggle_comment, "toggle comment");
    mapn("gf", kb_open_file_under_cursor, "open file");
    mapn("gF", kb_search_file_under_cursor, "search file");
    mapn("i", kb_enter_insert_mode, "insert");
    mapn("n", kb_search_next, "next match");
    mapn("p", kb_paste, "paste after");
    mapn("P", kb_paste_before, "paste before");
    mapn("r", kb_replace_char, "replace char");
    mapn("x", kb_delete_char, "del char");
    mapn("yy", kb_yank_line, "yank line");
    mapn("za", kb_fold_toggle, "fold toggle");
    mapn("zc", kb_fold_close, "fold close");
    mapn("zM", kb_fold_close_all, "close all folds");
    mapn("zo", kb_fold_open, "fold open");
    mapn("zR", kb_fold_open_all, "open all folds");
    mapn("zz", buf_center_screen, "center screen");
    mapn("~", kb_toggle_case, "toggle case");
    mapn("D", kb_delete_to_line_end, "del to line end");
    mapn("C", buf_change_to_line_end, "change to line end");
    mapn("S", buf_change_line, "change line");
    return 0;
}

const Plugin plugin_vim_keybinds = {
    .name   = "vim_keybinds",
    .desc   = "default Vim-style modal keymap",
    .init   = vim_keybinds_init,
    .deinit = NULL,
};
