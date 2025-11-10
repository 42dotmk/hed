#include "hed.h"

#define mapn(x, y) keybind_register(MODE_NORMAL, x, y)
#define mapi(x, y) keybind_register(MODE_INSERT, x, y)
#define mapv(x, y) keybind_register(MODE_VISUAL, x, y)
#define cmapn(x, y) keybind_register_command(MODE_NORMAL, x, y);

#define cm(x, y) command_register(x, y)


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

void user_keybinds_init(void) {
    mapn("i", kb_enter_insert_mode);
    mapn("a", kb_append_mode);
    mapn("A", kb_end_append);
    mapn("I", kb_start_insert);
    mapn("v", kb_enter_visual_mode);
    mapn(":", kb_enter_command_mode);
    mapn("dd", kb_delete_line);
    mapn("yy", kb_yank_line);
    mapn("p", kb_paste);
    mapn("x", kb_delete_char);
    mapn("0", kb_cursor_line_start);
    mapn("$", kb_cursor_line_end);
    mapn("gg", kb_cursor_top);
    mapn("G", kb_cursor_bottom);
    mapn("w", buf_cursor_move_word_forward);
    mapn("b", buf_cursor_move_word_backward);
    mapn("n", kb_search_next);
    mapn("u", kb_undo);
    mapn("<C-r>", kb_redo);
    cmapn(" fs", "w");
    cmapn(" ff", "fzf");
    cmapn(" bb", "ls");

    cmapn("<C-o>", "copen");
    cmapn("<C-q>", "cclose");
    cmapn("<C-n>", "cnext");
    cmapn("<C-p>", "cprev");
    cmapn(" rm", "shq make");

//TODO: implement these later
//  mapn(" qq", kb_quit_all);
//  mapn("e", kb_move_end_of_word);
//  mapn("*", kb_find_under_cursor);
//  mapn("O", kb_new_line_above);
//  mapn("o", kb_new_line_below);
//  mapn(" fr", kb_find_recent);
    /* New text-object deletions implemented in buf_helpers */
    mapn("da", buf_delete_around_char);
    mapn("di", buf_delete_inside_char);

    // visual mode
    mapv("y", kb_visual_yank);
    mapv("d", kb_visual_delete);

    mapn("tl", kb_line_number_toggle); 

}
void user_commands_init(void) { 
    cm("q", cmd_quit);
    cm("q!", cmd_quit_force);
    cm("quit", cmd_quit);
    cm("w", cmd_write);
    cm("wq", cmd_write_quit);
    cm("bn", cmd_buffer_next);
    cm("bp", cmd_buffer_prev);
    cm("ls", cmd_buffer_list);
    cm("b", cmd_buffer_switch);
    cm("bd", cmd_buffer_delete);
    cm("e", cmd_edit);
    cm("c", cmd_list_commands);
    cm("echo", cmd_echo);
    cm("history", cmd_history);
    cm("reg", cmd_registers);
    cm("regs", cmd_registers);
    cm("put", cmd_put);
    cm("undo", cmd_undo);
    cm("redo", cmd_redo);
    cm("ln", cmd_ln);
    cm("rln", cmd_rln);
    cm("copen", cmd_copen);
    cm("cclose", cmd_cclose);
    cm("ctoggle", cmd_ctoggle);
    cm("cadd", cmd_cadd);
    cm("cclear", cmd_cclear);
    cm("cnext", cmd_cnext);
    cm("cprev", cmd_cprev);
    cm("copenidx", cmd_copenidx);
    cm("rg", cmd_rg);
    cm("shq", cmd_shq);
    cm("fzf", cmd_fzf);
}

void user_hooks_init(void) {
    hook_register_mode(HOOK_MODE_CHANGE, on_mode_change);
}
