#include "hed.h"
#include "hooks.h"
#include "cmd_builtins.h"
#include "keybinds_builtins.h"

static void kb_del_win(char direction);
static void kb_del_up() { kb_del_win('k'); }
static void kb_del_down() { kb_del_win('j'); }
static void kb_del_left() { kb_del_win('h'); }
static void kb_del_right() { kb_del_win('l'); }

static void kb_end_append(void) {
    kb_cursor_line_end();
    kb_append_mode();
}

static void kb_start_insert(void) {
    kb_cursor_line_start();
    kb_enter_insert_mode();
}

static void on_mode_change(const HookModeEvent *event) {
    ed_change_cursor_shape();
    (void)event;
}

static void on_auto_pair(const HookCharEvent *event);

static void on_smart_indent(const HookCharEvent *event);

static void on_insert_char(const HookCharEvent *event) {
    on_auto_pair(event);
    on_smart_indent(event);
}

static void on_auto_pair(const HookCharEvent *event) {
    switch (event->c) {
    case '(':
        buf_insert_char_in(event->buf, ')');
        window_cur()->cursor.x--;
        break;
    case '[':
        buf_insert_char_in(event->buf, ']');
        window_cur()->cursor.x--;
        break;
    case '<':
        buf_insert_char_in(event->buf, '>');
        window_cur()->cursor.x--;
        break;
    case '{':
        buf_insert_char_in(event->buf, '}');
        window_cur()->cursor.x--;
        break;
	// TODO: Because we are raising the event again it goes into loop, 
	// TOOD: we need to figer on how to identify the sender better, and not make hacks
    // case '"':
    //     buf_insert_char_in(event->buf, '"');
    //     window_cur()->cursor.x--;
    //     break;
    // case '\'':
    //     buf_insert_char_in(event->buf, '\'');
    //     break;
    // case '`':
    //     buf_insert_char_in(event->buf, '`');
    //     window_cur()->cursor.x--;
    //     break;
    //     break;
    default:
        break;
    }
}

static void on_smart_indent(const HookCharEvent *event) {
    if (event->c == '\n') {
        Window *win = window_cur();
        if (!win)
            return;
        if (win->cursor.y == 0)
            return;
        Buffer *buf = event->buf;
        if (!buf)
            return;
        Row *prev_row = &buf->rows[win->cursor.y - 1];
        int prev_indent = 0;
        for (size_t i = 0; i < prev_row->chars.len; i++) {
            if (prev_row->chars.data[i] == ' ')
                prev_indent++;
            else if (prev_row->chars.data[i] == '\t')
                prev_indent += 4; // assuming tab width of 4
            else
                break;
        }
        for (int i = 0; i < prev_indent; i++) {
            buf_insert_char_in(buf, ' ');
        }
    }
}

void user_hooks_init(void) {
    hook_register_mode(HOOK_MODE_CHANGE, on_mode_change);
    hook_register_char(HOOK_CHAR_INSERT, MODE_INSERT, "*", on_insert_char);
    dired_hooks_init();
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
    cmd("rln", cmd_rln, "rel nums");
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
}

void imode_bindings() {
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
void nmode_bindings() {
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
    mapn("h", kb_move_left, "left");
    mapn("j", kb_move_down, "down");
    mapn("k", kb_move_up, "up");
    mapn("l", kb_move_right, "right");
    mapn("<Left>", kb_move_left, "left");
    mapn("<Down>", kb_move_down, "down");
    mapn("<Up>", kb_move_up, "up");
    mapn("<Right>", kb_move_right, "right");
    mapn("/", kb_search_prompt, "search");
    mapn("-", kb_dired_parent, "dired parent");
    mapn("~", kb_dired_home, "dired home");
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
    mapn("$", kb_cursor_line_end, "line end");
    mapv("$", kb_cursor_line_end, "line end");
    mapvb("$", kb_cursor_line_end, "line end");
    mapn("%", buf_find_matching_bracket, "match bracket");
    mapv("%", buf_find_matching_bracket, "match bracket");
    mapvb("%", buf_find_matching_bracket, "match bracket");
    mapn("*", kb_find_under_cursor, "find word");
    mapn("<C-*>", kb_find_under_cursor, "find word");
    mapn("0", kb_cursor_line_start, "line start");
    mapv("0", kb_cursor_line_start, "line start");
    mapvb("0", kb_cursor_line_start, "line start");
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
    mapn("G", kb_cursor_bottom, "goto bottom");
    mapn("I", kb_start_insert, "insert bol");
    mapn("J", buf_join_lines, "join lines");
    mapn("a", kb_append_mode, "append");
    mapn("b", buf_cursor_move_word_backward, "word back");
    mapn("cc", buf_toggle_comment, "toggle comment");
    mapn("da", buf_delete_around_char, "del around");
    mapn("db", buf_delete_word_backward, "del word back");
    mapn("dd", kb_delete_line, "del line");
    mapn("di", buf_delete_inside_char, "del inside");
    mapn("diw", buf_delete_inner_word, "del word");
    mapn("ci", buf_change_inside_char, "chg inside");
    mapn("cw", kb_change_word, "chg word");
    mapn("dp", buf_delete_paragraph, "del para");
    mapn("dw", buf_delete_word_forward, "del word fwd");
    mapn("gg", kb_cursor_top, "goto top");

    mapn("gf", kb_open_file_under_cursor, "open file");
    mapn("gF", kb_search_file_under_cursor, "search file");

    mapn("i", kb_enter_insert_mode, "insert");
    mapn("n", kb_search_next, "next match");
    mapn("p", kb_paste, "paste");
    mapn("r", kb_replace_char, "replace char");
    mapn("v", kb_visual_toggle, "visual");
    mapn("w", buf_cursor_move_word_forward, "word fwd");
    mapn("x", kb_delete_char, "del char");
    mapn("yp", buf_yank_paragraph, "yank para");
    mapn("yw", buf_yank_word, "yank word");
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
    nmode_bindings();
    imode_bindings();
}

static void kb_del_win(char direction) {
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
