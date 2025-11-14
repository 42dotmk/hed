#include "hed.h"

#define mapn(x, y) keybind_register(MODE_NORMAL, x, y)
#define mapi(x, y) keybind_register(MODE_INSERT, x, y)
#define mapv(x, y) keybind_register(MODE_VISUAL, x, y)
#define cmapn(x, y) keybind_register_command(MODE_NORMAL, x, y);
#define cm(name, cb, desc) command_register(name, cb, desc)

/* Forward declarations for binding groups */
void imode_bindings(void);
void nmode_bindings(void);
void vmode_bindings(void);

static void on_mode_change(const HookModeEvent *event) {
  ed_change_cursor_shape();
  (void)event;
}

static void kb_end_append(void) {
  kb_cursor_line_end();
  kb_append_mode();
}

static void kb_start_insert(void) {
  kb_cursor_line_start();
  kb_enter_insert_mode();
}

void user_hooks_init(void) {
  hook_register_mode(HOOK_MODE_CHANGE, on_mode_change);
}

void user_commands_init(void) {
  cm("q", cmd_quit, "quit");
  cm("q!", cmd_quit_force, "quit!\n");
  cm("quit", cmd_quit, "quit");
  cm("w", cmd_write, "write");
  cm("wq", cmd_write_quit, "write+quit");
  cm("bn", cmd_buffer_next, "next buf");
  cm("bp", cmd_buffer_prev, "prev buf");
  cm("ls", cmd_buffer_list, "list bufs");
  cm("b", cmd_buffer_switch, "switch buf");
  cm("bd", cmd_buffer_delete, "delete buf");
  cm("e", cmd_edit, "edit file");
  cm("c", cmd_cpick, "pick cmd");
  cm("echo", cmd_echo, "echo");
  cm("history", cmd_history, "cmd hist");
  cm("reg", cmd_registers, "registers");
  cm("put", cmd_put, "put reg");
  cm("undo", cmd_undo, "undo");
  cm("redo", cmd_redo, "redo");
  cm("ln", cmd_ln, "line nums");
  cm("rln", cmd_rln, "rel nums");
  cm("copen", cmd_copen, "qf open");
  cm("cclose", cmd_cclose, "qf close");
  cm("ctoggle", cmd_ctoggle, "qf toggle");
  cm("cadd", cmd_cadd, "qf add");
  cm("cclear", cmd_cclear, "qf clear");
  cm("cnext", cmd_cnext, "qf next");
  cm("cprev", cmd_cprev, "qf prev");
  cm("copenidx", cmd_copenidx, "qf open N");
  cm("rg", cmd_rg, "ripgrep");
  cm("shq", cmd_shq, "shell cmd");
  cm("cd", cmd_cd, "chdir");
  cm("fzf", cmd_fzf, "file pick");
  cm("recent", cmd_recent, "recent files");
  cm("logclear", cmd_logclear, "clear .hedlog");
  cm("shell", cmd_shell, "run shell cmd");
  /* Syntax highlighting */
  cm("ts", cmd_ts, "ts on|off|auto");
  cm("tslang", cmd_tslang, "tslang <name>");
  /* Editing convenience */
  cm("new_line", cmd_new_line, "open new line below");
  cm("new_line_above", cmd_new_line_above, "open new line above");
  /* Windows */
  cm("split", cmd_split, "horizontal split");
  cm("vsplit", cmd_vsplit, "vertical split");
  cm("wfocus", cmd_wfocus, "focus next window");
  cm("wclose", cmd_wclose, "close window");
}

void user_keybinds_init(void) {
  nmode_bindings();
  imode_bindings();
  vmode_bindings();
}
void imode_bindings() {}
void nmode_bindings() {
  cmapn("  ", "fzf");
  cmapn(" bb", "ls");
  cmapn(" ff", "fzf");
  cmapn(" fr", "recent");
  cmapn(" fs", "w");
  cmapn(" qq", "q!");
  cmapn(" rm", "shell make");
  cmapn(" sd", "rg");
  cmapn(" ss", "ssearch");
  cmapn(" tl", "ln");
  cmapn(" wd", "wclose");
  cmapn(" ws", "split");
  cmapn(" wv", "vsplit");
  cmapn(" ww", "wfocus");

  cmapn("<C-.>", "ctoggle");
  cmapn("<C-n>", "cnext");
  cmapn("<C-p>", "cprev");
  cmapn("<C-r>", "redo");
  cmapn("O", "new_line_above");
  cmapn("o", "new_line");
  cmapn("u", "undo");

  mapn("$", kb_cursor_line_end);
  mapn("%", buf_find_matching_bracket);
  mapn("*", kb_find_under_cursor);
  mapn("0", kb_cursor_line_start);
  mapn(":", kb_enter_command_mode);
  mapn("<<", buf_unindent_line);
  mapn("<C-d>", buf_scroll_half_page_down);
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
  mapn("dp", buf_delete_paragraph);
  mapn("dw", buf_delete_word_forward);
  mapn("gg", kb_cursor_top);
  mapn("i", kb_enter_insert_mode);
  mapn("n", kb_search_next);
  mapn("p", kb_paste);
  mapn("v", kb_enter_visual_mode);
  mapn("w", buf_cursor_move_word_forward);
  mapn("x", kb_delete_char);
  mapn("yp", buf_yank_paragraph);
  mapn("yw", buf_yank_word);
  mapn("yy", kb_yank_line);
  mapn("zz", buf_center_screen);
}

void vmode_bindings() {
  /* Core operations */
  mapv("y", visual_yank);
  mapv("d", visual_delete);
  mapv("x", visual_delete);
  mapv("c", visual_change);

  /* Indentation */
  mapv(">", visual_indent);
  mapv("<", visual_unindent);

  /* Case operations */
  mapv("~", visual_toggle_case);
  mapv("u", visual_lowercase);
  mapv("U", visual_uppercase);

  /* Selection helpers (expand selection) */
  mapv("w", buf_select_word);
  mapv("p", buf_select_paragraph);
  mapv("V", buf_select_line);
}
