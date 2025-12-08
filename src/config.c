#include "hed.h"
#include "safe_string.h"

#define mapn(x, y) keybind_register(MODE_NORMAL, x, y)
#define mapi(x, y) keybind_register(MODE_INSERT, x, y)
#define cmapn(x, y) keybind_register_command(MODE_NORMAL, x, y);

/* Forward declarations for binding groups */
void imode_bindings(void);
void nmode_bindings(void);

static void on_mode_change(const HookModeEvent *event) {
  ed_change_cursor_shape();
  (void)event;
}

static void kb_del_win(char direction) {
  switch (direction) {
  case 'h':
    windows_focus_left();
    cmd_wclose(NULL);
    break;
  case 'j':
    windows_focus_down();
    cmd_wclose(NULL);
    break;
  case 'k':
    windows_focus_up();
    cmd_wclose(NULL);
    break;
  case 'l':
    windows_focus_right();
    cmd_wclose(NULL);
    break;
  }
}
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

typedef struct {
  const char *name;
  CommandCallback cb;
  const char *desc;
} CommandDef;

static const CommandDef command_defs[] = {
    {"q", cmd_quit, "quit"},
    {"q!", cmd_quit_force, "quit!\n"},
    {"quit", cmd_quit, "quit"},
    {"w", cmd_write, "write"},
    {"wq", cmd_write_quit, "write+quit"},
    {"bn", cmd_buffer_next, "next buf"},
    {"bp", cmd_buffer_prev, "prev buf"},
    {"ls", cmd_buffer_list, "list bufs"},
    {"b", cmd_buffer_switch, "switch buf"},
    {"bd", cmd_buffer_delete, "delete buf"},
    {"e", cmd_edit, "edit file"},
    {"c", cmd_cpick, "pick cmd"},
    {"echo", cmd_echo, "echo"},
    {"history", cmd_history, "cmd hist"},
    {"reg", cmd_registers, "registers"},
    {"put", cmd_put, "put reg"},
    {"undo", cmd_undo, "undo"},
    {"redo", cmd_redo, "redo"},
    {"ln", cmd_ln, "line nums"},
    {"rln", cmd_rln, "rel nums"},
    {"copen", cmd_copen, "qf open"},
    {"cclose", cmd_cclose, "qf close"},
    {"ctoggle", cmd_ctoggle, "qf toggle"},
    {"cadd", cmd_cadd, "qf add"},
    {"cclear", cmd_cclear, "qf clear"},
    {"cnext", cmd_cnext, "qf next"},
    {"cprev", cmd_cprev, "qf prev"},
    {"copenidx", cmd_copenidx, "qf open N"},
    {"ssearch", cmd_ssearch, "search current file"},
    {"rgword", cmd_rg_word, "ripgrep word under cursor"},
    {"rg", cmd_rg, "ripgrep"},
    {"shq", cmd_shq, "shell cmd"},
    {"cd", cmd_cd, "chdir"},
    {"fzf", cmd_fzf, "file pick"},
    {"recent", cmd_recent, "recent files"},
    {"logclear", cmd_logclear, "clear .hedlog"},
    {"shell", cmd_shell, "run shell cmd"},
    {"git", cmd_git, "run lazygit"},
    {"wrap", cmd_wrap, "toggle wrap"},
    {"wrapdefault", cmd_wrapdefault, "toggle default wrap"},
    {"tmux_toggle", cmd_tmux_toggle, "tmux toggle runner pane"},
    {"tmux_send", cmd_tmux_send, "tmux send command"},
    {"tmux_kill", cmd_tmux_kill, "tmux kill runner pane"},
    {"fmt", cmd_fmt, "format buffer"},
    {"new_line", cmd_new_line, "open new line below"},
    {"new_line_above", cmd_new_line_above, "open new line above"},
    {"split", cmd_split, "horizontal split"},
    {"vsplit", cmd_vsplit, "vertical split"},
    {"wfocus", cmd_wfocus, "focus next window"},
    {"wclose", cmd_wclose, "close window"},
    {"new", cmd_new, "new split with empty buffer"},
    {"wh", cmd_wleft, "focus window left"},
    {"wj", cmd_wdown, "focus window down"},
    {"wk", cmd_wup, "focus window up"},
    {"wl", cmd_wright, "focus window right"},
    {"ts", cmd_ts, "ts on|off|auto"},
    {"tslang", cmd_tslang, "tslang <name>"},
    {"tsi", cmd_tsi, "install ts lang"},
    {"reload", cmd_reload, "rebuild+restart hed"},
};

void user_hooks_init(void) {
  hook_register_mode(HOOK_MODE_CHANGE, on_mode_change);
}

void user_commands_init(void) {
  for (size_t i = 0; i < sizeof(command_defs) / sizeof(command_defs[0]); i++) {
    command_register(command_defs[i].name, command_defs[i].cb,
                     command_defs[i].desc);
  }
}

void user_keybinds_init(void) {
  nmode_bindings();
  imode_bindings();
}
void imode_bindings() {}
void nmode_bindings() {
  cmapn("  ", "fzf");
  cmapn(" bb", "ls");
  cmapn(" ff", "fzf");
  cmapn(" fn", "new");
  cmapn(" fr", "recent");
  cmapn(" fs", "w");
  cmapn(" cf", "fmt");
  cmapn(" qq", "q!");
  cmapn(" rm", "shell make");
  cmapn(" de", "shell yazzi");
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
  mapn("%", buf_find_matching_bracket);
  mapn("*", kb_find_under_cursor);
  mapn("0", kb_cursor_line_start);
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

/* No visual mode bindings */
