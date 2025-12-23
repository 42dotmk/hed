#ifndef KEYBINDS_BUILTINS_H
#define KEYBINDS_BUILTINS_H

/*
 * BUILT-IN KEYBIND CALLBACKS
 * ==========================
 *
 * Convenience header that exposes the default keybinding actions.
 * Include this alongside keybinds.h when you need to bind or call
 * the built-in behaviors (e.g., config and editor modules).
 */

#include "ui/window.h"

void kb_line_number_toggle(void);
void kb_enter_insert_mode(void);
void kb_append_mode(void);
void kb_enter_command_mode(void);
void kb_delete_line(void);
void kb_yank_line(void);
void kb_paste(void);
void kb_delete_char(void);
void kb_cursor_line_start(void);
void kb_cursor_line_end(void);
void kb_cursor_top(void);
void kb_cursor_bottom(void);
void kb_search_next(void);
void kb_find_under_cursor(void);
void kb_search_file_under_cursor(void);
void kb_open_file_under_cursor(void);
void kb_undo(void);
void kb_redo(void);
void kb_fzf(void);
void kb_quit_all(void);
void kb_jump_backward(void);  /* Ctrl-O - jump to previous buffer */
void kb_jump_forward(void);   /* Ctrl-I - jump to next buffer */
void kb_tmux_send_line(void); /* Send current line to tmux runner pane */
void kb_move_left(void);
void kb_move_right(void);
void kb_move_up(void);
void kb_move_down(void);
void kb_insert_newline(void);
void kb_insert_tab(void);
void kb_insert_backspace(void);
void kb_insert_escape(void);
void kb_dired_enter(void);
void kb_dired_parent(void);
void kb_dired_home(void);

void kb_visual_yank_selection(void);
void kb_visual_delete_selection(void);
void kb_visual_escape(void);
void kb_visual_toggle_block_mode(void);
void kb_visual_enter_insert_mode(void);
void kb_visual_enter_append_mode(void);
void kb_visual_enter_command_mode(void);
void kb_search_prompt(void);
void kb_visual_toggle(void);       /* Enter/exit visual mode */
void kb_visual_block_toggle(void); /* Enter/exit visual block mode */
void kb_visual_clear(Window *win);
void kb_visual_begin(int block_mode);
int kb_visual_yank(Buffer *buf, Window *win, int block_mode);
int kb_visual_delete(Buffer *buf, Window *win, int block_mode);

void kb_change_word(void); /* Change word (cw): delete to end of word and enter insert mode */
void kb_toggle_case(void); /* Toggle case of char under cursor (~) */
void kb_replace_char(void); /* Replace char under cursor with next typed char (r) */

/* Fold operations */
void kb_fold_toggle(void);    /* za - Toggle fold at cursor */
void kb_fold_open(void);      /* zo - Open fold at cursor */
void kb_fold_close(void);     /* zc - Close fold at cursor */
void kb_fold_open_all(void);  /* zR - Open all folds */
void kb_fold_close_all(void); /* zM - Close all folds */

#endif /* KEYBINDS_BUILTINS_H */
