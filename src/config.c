#include "hed.h"
#define mapn(x, y) keybind_register(MODE_NORMAL, x, y)
#define mapi(x, y) keybind_register(MODE_INSERT, x, y)
#define mapv(x, y) keybind_register(MODE_VISUAL, x, y)
#define cm(x, y) command_register(x, y)

static void on_mode_change(const HookModeEvent *event) {
    ed_change_cursor_shape();
    (void)event;
}
void user_keybinds_init(void) {
    mapn("ll", buf_cursor_move_top);
    mapn("i", kb_enter_insert_mode);
    mapn("a", kb_append_mode);
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

    // visual mode
    mapv("y", kb_visual_yank);
    mapv("d", kb_visual_delete);
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
}

void user_hooks_init(void) {
    hook_register_mode(HOOK_MODE_CHANGE, on_mode_change);
}
