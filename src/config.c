#include "hed.h"
#include "hooks.h"
#include "cmd_builtins.h"
#include "keybinds_builtins.h"
#include "hook_builtins.h"
#include "lsp_hooks.h"


void insert_mode_bindings() {
    mapi("<Esc>", kb_insert_escape, "exit insert");
    mapi("<CR>", kb_insert_newline, "new line");
    mapi("<Tab>", kb_insert_tab, "insert tab");
    mapi("<BS>", kb_insert_backspace, "backspace");
    mapi("<C-h>", kb_insert_backspace, "backspace");
    mapi("<Up>", kb_move_up, "move up");
    mapi("<Down>", kb_move_down, "move down");
    mapi("<Left>", kb_move_left, "move left");
    mapi("<Right>", kb_move_right, "move right");
}

void normal_mode_bindings() {
    cmapn("  ", "fzf");
    cmapn(" bb", "ls");
    cmapn(" bd", "bd");
    cmapn(" br", "refresh");
    cmapn(" ff", "fzf");
    cmapn(" fn", "new");
    cmapn(" fr", "recent");
    cmapn(" fs", "w");
	cmapn(" fc", "c");
	cmapn(" fm", "keybinds");
    cmapn(" cf", "fmt");
    cmapn(" qq", "q!");
    cmapn(" rm", "shell make");
    cmapn(" mm", "shell make");
    cmapn(" mt", "shell make test");
    cmapn(" nn", "shell --skipwait nnn");
    cmapn(" dd", "e .");
    cmapn(" sd", "rg");
    cmapn(" ss", "ssearch");
    cmapn(" sa", "rgword");
    cmapn("<C-*>", "rgword");
    cmapn(" tw", "wrap");
    cmapn(" tt", "tmux_toggle");
    cmapn(" tT", "tmux_kill");
    cmapn(" gg", "git");
    cmapn(" tl", "ln");
    cmapn(" wd", "wclose");
    cmapn(" ws", "split");
    cmapn(" wv", "vsplit");
    cmapn(" ww", "wfocus");
    cmapn(" wh", "wh");
    cmapn(" wj", "wj");
    cmapn(" wk", "wk");
    cmapn(" wl", "wl");
    /* h/j/k/l and arrow keys now handled by text object fallback */
    mapn("/", kb_search_prompt, "search");
    mapn("-", kb_dired_parent, "dired parent");
    mapn("~", kb_dired_home, "dired home");
    mapn("cd", kb_dired_chdir, "dired chdir");
    mapn("<CR>", kb_dired_enter, "dired open");
    mapn(" ts", kb_tmux_send_line, "send to tmux");
    mapn(" dl", kb_del_right, "del win right");
    mapn(" d1", kb_del_left, "del win left");
    mapn(" dj", kb_del_down, "del win down");
    mapn(" dk", kb_del_up, "del win up");
    cmapn(" rr", "reload");

    cmapn(" tq", "ctoggle");
    cmapn("<C-n>", "cnext");
    cmapn("gn", "cnext");
    cmapn("<C-p>", "cprev");
    cmapn("gp", "cprev");
    cmapn("gr", "rgword");
    cmapn("gd", "tag");
    cmapn("<C-r>", "redo");
    cmapn("O", "new_line_above");
    cmapn("o", "new_line");
    cmapn("U", "redo");
    cmapn("u", "undo");
    cmapn(".", "repeat");
    cmapn("q", "record");
    cmapn("@", "play");
    cmapn("ZQ", "q!");
    cmapn("ZZ", "wq"); // not quite the same as in neovim
                       // ZZ in neovim writes to disk only if changes are made
                       // whereas :wq always writes to disk

    /* $, 0, w, b, e movements now handled by text object fallback */
    mapn("%", buf_find_matching_bracket, "match bracket");
    mapv("%", buf_find_matching_bracket, "match bracket");
    mapvb("%", buf_find_matching_bracket, "match bracket");
    mapn("*", kb_find_under_cursor, "find word");
    mapn("<C-*>", kb_find_under_cursor, "find word");
    mapv("h", kb_move_left, "left");
    mapv("j", kb_move_down, "down");
    mapv("k", kb_move_up, "up");
    mapv("l", kb_move_right, "right");
    mapv("<Left>", kb_move_left, "left");
    mapv("<Down>", kb_move_down, "down");
    mapv("<Up>", kb_move_up, "up");
    mapv("<Right>", kb_move_right, "right");
    mapvb("h", kb_move_left, "left");
    mapvb("j", kb_move_down, "down");
    mapvb("k", kb_move_up, "up");
    mapvb("l", kb_move_right, "right");
    mapvb("<Left>", kb_move_left, "left");
    mapvb("<Down>", kb_move_down, "down");
    mapvb("<Up>", kb_move_up, "up");
    mapvb("<Right>", kb_move_right, "right");
    mapv("y", kb_visual_yank_selection, "yank");
    mapvb("y", kb_visual_yank_selection, "yank");
    mapv("d", kb_visual_delete_selection, "delete");
    mapvb("d", kb_visual_delete_selection, "delete");
    mapv("v", kb_visual_escape, "exit visual");
    mapvb("v", kb_visual_escape, "exit visual");
    mapv("<C-v>", kb_visual_toggle_block_mode, "block mode");
    mapvb("<C-v>", kb_visual_toggle_block_mode, "block mode");
    mapv("<Esc>", kb_visual_escape, "exit visual");
    mapvb("<Esc>", kb_visual_escape, "exit visual");
    mapv("i", kb_visual_enter_insert_mode, "insert");
    mapvb("i", kb_visual_enter_insert_mode, "insert");
    mapv("a", kb_visual_enter_append_mode, "append");
    mapvb("a", kb_visual_enter_append_mode, "append");
    mapv(":", kb_visual_enter_command_mode, "command");
    mapvb(":", kb_visual_enter_command_mode, "command");
    mapn(":", kb_enter_command_mode, "command");
    mapn("<<", buf_unindent_line, "unindent");
    mapn("<C-d>", buf_scroll_half_page_down, "scroll down");
    mapn("<C-v>", kb_visual_block_toggle, "visual block");
    mapn(" jf", kb_jump_forward, "jump forward");
    mapn(" jb", kb_jump_backward, "jump back");
    mapn("<C-i>", kb_jump_forward, "jump forward");
    mapn("<C-o>", kb_jump_backward, "jump back");
    mapn("<C-u>", buf_scroll_half_page_up, "scroll up");
    mapn(">>", buf_indent_line, "indent");
    mapn("A", kb_end_append, "append eol");
    mapn("I", kb_start_insert, "insert bol");
    mapn("J", buf_join_lines, "join lines");
    mapn("a", kb_append_mode, "append");

    /* Operator keybindings - wait for text object input */
    mapn("d", kb_operator_delete, "delete operator");
    mapn("c", kb_operator_change, "change operator");
    mapn("y", kb_operator_yank, "yank operator");
    mapn("v", kb_operator_select, "visual select with motion");

    /* Special cases: operator on same key acts on line */
    mapn("dd", kb_delete_line, "del line");
    /* Note: cc conflicts with comment toggle - moved comment to gc */
    /* Note: G, gg now handled by text object fallback */
    mapn("gc", buf_toggle_comment, "toggle comment");

    mapn("gf", kb_open_file_under_cursor, "open file");
    mapn("gF", kb_search_file_under_cursor, "search file");

    mapn("i", kb_enter_insert_mode, "insert");
    mapn("n", kb_search_next, "next match");
    mapn("p", kb_paste, "paste");
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
}

void user_keybinds_init(void) {
    normal_mode_bindings();
    insert_mode_bindings();
}

void user_textobj_init(void) {
    log_msg("Initializing user text objects");

    /* Basic movement text objects (hjkl and arrows) */
    textobj_register("h", textobj_char_left, "char left");
    textobj_register("j", textobj_line_down, "line down");
    textobj_register("k", textobj_line_up, "line up");
    textobj_register("l", textobj_char_right, "char right");

    /* Motion text objects (single char) */
    textobj_register("w", textobj_to_word_end, "word forward");
    textobj_register("b", textobj_to_word_start, "word backward");
    textobj_register("e", textobj_to_word_end, "word end");
    textobj_register("$", textobj_to_line_end, "end of line");
    textobj_register("0", textobj_to_line_start, "beginning of line");
    textobj_register("G", textobj_to_file_end, "end of file");
    textobj_register("gg", textobj_to_file_start, "start of file");

    /* Inner text objects (two char: i + object) */
    textobj_register("iw", textobj_word, "inner word");
    textobj_register("ip", textobj_paragraph, "inner paragraph");
    textobj_register("i(", textobj_brackets, "inner parentheses");
    textobj_register("i)", textobj_brackets, "inner parentheses");
    textobj_register("ib", textobj_brackets, "inner brackets");
    /* Note: braces, square brackets, quotes require textobj_brackets_with wrapper */

    /* Around text objects (two char: a + object) */
    textobj_register("aw", textobj_word, "around word");
    textobj_register("ap", textobj_paragraph, "around paragraph");
    /* Note: More around objects will be added when textobj_brackets_with wrappers are ready */
}

void user_commands_init(void) {
    cmd("q", cmd_quit, "quit");
    cmd("q!", cmd_quit_force, "quit!");
    cmd("quit", cmd_quit, "quit");
    cmd("w", cmd_write, "write");
    cmd("wq", cmd_write_quit, "write+quit");
    cmd("bn", cmd_buffer_next, "next buf");
    cmd("bp", cmd_buffer_prev, "prev buf");
    cmd("ls", cmd_buffer_list, "list bufs");
	cmd("refresh", cmd_buf_refresh, "refresh contents");
    cmd("b", cmd_buffer_switch, "switch buf");
    cmd("bd", cmd_buffer_delete, "delete buf");
    cmd("e", cmd_edit, "edit file");
    cmd("c", cmd_cpick, "pick cmd");
    cmd("keybinds", cmd_list_keybinds, "list keybinds");
    cmd("echo", cmd_echo, "echo");
    cmd("history", cmd_history, "cmd hist");
    cmd("reg", cmd_registers, "registers");
    cmd("put", cmd_put, "put reg");
    cmd("undo", cmd_undo, "undo");
    cmd("redo", cmd_redo, "redo");
    cmd("repeat", cmd_repeat, "repeat last");
    cmd("record", cmd_macro_record, "record macro");
    cmd("play", cmd_macro_play, "play macro");
    cmd("ln", cmd_ln, "line nums");
    cmd("rln", cmd_rln, "relative numbers");
    cmd("copen", cmd_copen, "qf open");
    cmd("cclose", cmd_cclose, "qf close");
    cmd("ctoggle", cmd_ctoggle, "qf toggle");
    cmd("cadd", cmd_cadd, "qf add");
    cmd("cclear", cmd_cclear, "qf clear");
    cmd("cnext", cmd_cnext, "qf next");
    cmd("cprev", cmd_cprev, "qf prev");
    cmd("copenidx", cmd_copenidx, "qf open N");
    cmd("ssearch", cmd_ssearch, "search current file");
    cmd("rgword", cmd_rg_word, "ripgrep word under cursor");
    cmd("rg", cmd_rg, "ripgrep");
    cmd("tag", cmd_tag, "jump to tag definition");
    cmd("shq", cmd_shq, "shell cmd");
    cmd("sed", cmd_sed, "apply sed expression to buffer");
    cmd("cd", cmd_cd, "chdir");
    cmd("pwd", cmd_cd, "current dir");
    cmd("fzf", cmd_fzf, "pick a file(s)");
    cmd("recent", cmd_recent, "recent files");
    cmd("logclear", cmd_logclear, "clear .hedlog");
    cmd("shell", cmd_shell, "run shell cmd");
    cmd("git", cmd_git, "run lazygit");
    cmd("wrap", cmd_wrap, "toggle wrap");
    cmd("wrapdefault", cmd_wrapdefault, "toggle default wrap");
    cmd("tmux_toggle", cmd_tmux_toggle, "tmux toggle runner pane");
    cmd("tmux_send", cmd_tmux_send, "tmux send command");
    cmd("tmux_kill", cmd_tmux_kill, "tmux kill runner pane");
    cmd("fmt", cmd_fmt, "format buffer");
    cmd("new_line", cmd_new_line, "open new line below");
    cmd("new_line_above", cmd_new_line_above, "open new line above");
    cmd("split", cmd_split, "horizontal split");
    cmd("vsplit", cmd_vsplit, "vertical split");
    cmd("wfocus", cmd_wfocus, "focus next window");
    cmd("wclose", cmd_wclose, "close window");
    cmd("new", cmd_new, "new split with empty buffer");
    cmd("wh", cmd_wleft, "focus window left");
    cmd("wj", cmd_wdown, "focus window down");
    cmd("wk", cmd_wup, "focus window up");
    cmd("wl", cmd_wright, "focus window right");
    cmd("ts", cmd_ts, "ts on|off|auto");
    cmd("tslang", cmd_tslang, "tslang <name>");
    cmd("tsi", cmd_tsi, "install ts lang");
    cmd("reload", cmd_reload, "rebuild+restart hed");
    cmd("modal", cmd_modal_from_current, "convert current window to modal");
    cmd("unmodal", cmd_modal_to_layout, "convert modal back to normal window");
    cmd("foldnew", cmd_fold_new, "create fold region");
    cmd("foldrm", cmd_fold_rm, "remove fold at line");
    cmd("foldtoggle", cmd_fold_toggle, "toggle fold at line");
    cmd("foldmethod", cmd_foldmethod, "set fold method");
    cmd("foldupdate", cmd_foldupdate, "update folds");
    cmd("lsp_start", cmd_lsp_start, "start LSP server");
    cmd("lsp_stop", cmd_lsp_stop, "stop LSP server");
    cmd("lsp_hover", cmd_lsp_hover, "LSP hover info");
    cmd("lsp_definition", cmd_lsp_definition, "LSP goto definition");
    cmd("lsp_completion", cmd_lsp_completion, "LSP completion");
}

void user_hooks_init(void) {
    hook_register_mode(HOOK_MODE_CHANGE, hook_change_cursor_shape);
    hook_register_char(HOOK_CHAR_INSERT, MODE_INSERT, "*", hook_smart_indent);
    hook_register_char(HOOK_CHAR_INSERT, MODE_INSERT, "*", hook_auto_pair);
    dired_hooks_init();
    lsp_hooks_init();
}

