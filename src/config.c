#include "hed.h"
#include "safe_string.h"

#define cmd(name, cb, desc) command_register(name, cb, desc)
#define mapn(x, y) keybind_register(MODE_NORMAL, x, y)
#define mapv(x, y) keybind_register(MODE_VISUAL, x, y)
#define mapi(x, y) keybind_register(MODE_INSERT, x, y)
#define cmapn(x, y) keybind_register_command(MODE_NORMAL, x, y)
#define cmapv(x, y) keybind_register_command(MODE_VISUAL, x, y)

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

void user_hooks_init(void) {
  hook_register_mode(HOOK_MODE_CHANGE, on_mode_change);
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
  cmd("b", cmd_buffer_switch, "switch buf");
  cmd("bd", cmd_buffer_delete, "delete buf");
  cmd("e", cmd_edit, "edit file");
  cmd("c", cmd_cpick, "pick cmd");
  cmd("echo", cmd_echo, "echo");
  cmd("history", cmd_history, "cmd hist");
  cmd("reg", cmd_registers, "registers");
  cmd("put", cmd_put, "put reg");
  cmd("undo", cmd_undo, "undo");
  cmd("redo", cmd_redo, "redo");
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
  cmd("shq", cmd_shq, "shell cmd");
  cmd("cd", cmd_cd, "chdir");
  cmd("fzf", cmd_fzf, "file pick");
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
}

void imode_bindings() {}
void nmode_bindings() {
  cmapn("  ", "fzf");
  cmapn(" bb", "ls");
  cmapn(" bd", "bd");
  cmapn(" ff", "fzf");
  cmapn(" fn", "new");
  cmapn(" fr", "recent");
  cmapn(" fs", "w");
  cmapn(" cf", "fmt");
  cmapn(" qq", "q!");
  cmapn(" rm", "shell make");
  cmapn(" dd", "shell nnn");
  cmapn(" sd", "rg");
  cmapn(" ss", "ssearch");
  cmapn(" sa", "rgword");
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
  mapn(" ts", kb_tmux_send_line);
  mapn(" dl", kb_del_right);
  mapn(" d1", kb_del_left);
  mapn(" dj", kb_del_down);
  mapn(" dk", kb_del_up);
  cmapn(" rr", "reload");

  cmapn(" tq", "ctoggle");
  cmapn("<C-n>", "cnext");
  cmapn("gn", "cnext");
  cmapn("<C-p>", "cprev");
  cmapn("gp", "cprev");
  cmapn("<C-r>", "redo");
  cmapn("O", "new_line_above");
  cmapn("o", "new_line");
  cmapn("U", "redo");
  cmapn("u", "undo");
  mapn("$", kb_cursor_line_end);
  mapv("$", kb_cursor_line_end);
  mapn("%", buf_find_matching_bracket);
  mapv("%", buf_find_matching_bracket);
  mapn("*", kb_find_under_cursor);
  mapn("0", kb_cursor_line_start);
  mapv("0", kb_cursor_line_start);
  mapn(":", kb_enter_command_mode);
  mapn("<<", buf_unindent_line);
  mapn("<C-d>", buf_scroll_half_page_down);
  mapn("<C-v>", kb_visual_block_toggle);
  mapn("<C-i>", kb_jump_forward);
  mapn("<C-o>", kb_jump_backward);
  mapn("<C-u>", buf_scroll_half_page_up);
  mapn(">>", buf_indent_line);
  mapn("A", kb_end_append);
  mapn("G", kb_cursor_bottom);
  mapn("I", kb_start_insert);
  mapn("J", buf_join_lines);
  mapn("a", kb_append_mode);
  mapn("b", buf_cursor_move_word_backward);
  mapn("cc", buf_toggle_comment);
  mapn("da", buf_delete_around_char);
  mapn("db", buf_delete_word_backward);
  mapn("dd", kb_delete_line);
  mapn("di", buf_delete_inside_char);
  mapn("diw", buf_delete_inner_word);
  mapn("ci", buf_change_inside_char);
  mapn("dp", buf_delete_paragraph);
  mapn("dw", buf_delete_word_forward);
  mapn("gg", kb_cursor_top);
  mapn("i", kb_enter_insert_mode);
  mapn("n", kb_search_next);
  mapn("p", kb_paste);
  mapn("v", kb_visual_toggle);
  mapn("w", buf_cursor_move_word_forward);
  mapn("x", kb_delete_char);
  mapn("yp", buf_yank_paragraph);
  mapn("yw", buf_yank_word);
  mapn("yy", kb_yank_line);
  mapn("zz", buf_center_screen);
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
