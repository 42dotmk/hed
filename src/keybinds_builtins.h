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
#include "buf/textobj.h"

void kb_line_number_toggle(void);
void kb_enter_insert_mode(void);
void kb_append_mode(void);
void kb_enter_command_mode(void);
void kb_delete_line(void);
void kb_delete_to_line_end(void);
void kb_yank_line(void);
void kb_paste(void);
void kb_paste_before(void);
void kb_delete_char(void);
/* Operator functions (wait for text object input) */
void kb_operator_delete(void);
void kb_operator_change(void);
void kb_operator_yank(void);
void kb_operator_move(int key);    /* Move cursor via text object (fallback) */
void kb_operator_select(void);     /* Visual select via text object (v + motion) */
/* Note: cursor movements now handled by text object system */
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
void kb_goto_file_start(void); /* gg - go to first line */
void kb_para_next(void);       /* }  - next paragraph  */
void kb_para_prev(void);       /* {  - prev paragraph  */
void kb_move_left(void);
void kb_move_right(void);
void kb_move_up(void);
void kb_move_down(void);

/* Cursor motions that delegate to text-objects (no mode change). Used
 * by modeless keymap plugins. */
void kb_goto_line_start(void);
void kb_goto_line_end(void);
void kb_goto_file_start(void);
void kb_goto_file_end(void);
void kb_goto_word_start(void);
void kb_goto_word_end(void);
void kb_goto_para_start(void);
void kb_goto_para_end(void);

/* Selection-aware variants used by modeless keymaps. `kb_drop_*` exits
 * any active visual selection and then moves; `kb_extend_*` enters
 * visual mode (if not already) and then moves, extending the selection. */
void kb_drop_left(void);
void kb_drop_right(void);
void kb_drop_up(void);
void kb_drop_down(void);
void kb_drop_word_l(void);
void kb_drop_word_r(void);
void kb_drop_bol(void);
void kb_drop_eol(void);
void kb_extend_left(void);
void kb_extend_right(void);
void kb_extend_up(void);
void kb_extend_down(void);
void kb_extend_word_l(void);
void kb_extend_word_r(void);
void kb_extend_bol(void);
void kb_extend_eol(void);
void kb_insert_newline(void);
void kb_insert_tab(void);
void kb_insert_backspace(void);
void kb_insert_escape(void);

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
void kb_visual_line_toggle(void);  /* Enter/exit visual line mode */
void kb_visual_clear(Window *win);
void kb_visual_begin(int block_mode);
int kb_visual_yank(Buffer *buf, Window *win, int block_mode);
int kb_visual_delete(Buffer *buf, Window *win, int block_mode);
/* Build a TextSelection from the window's current visual selection.
 * `block_mode` is forced on if win->sel.type is SEL_VISUAL_BLOCK.
 * Returns 1 on success and fills *out; 0 if there is no selection. */
int kb_visual_to_textsel(Buffer *buf, Window *win, int block_mode,
                         TextSelection *out);

/* Note: kb_change_word removed - now handled by operator + text object system */
void kb_toggle_case(void); /* Toggle case of char under cursor (~) */
void kb_replace_char(void); /* Replace char under cursor with next typed char (r) */

/* Fold operations */
void kb_fold_toggle(void);    /* za - Toggle fold at cursor */
void kb_fold_open(void);      /* zo - Open fold at cursor */
void kb_fold_close(void);     /* zc - Close fold at cursor */
void kb_fold_open_all(void);  /* zR - Open all folds */
void kb_fold_close_all(void); /* zM - Close all folds */

void kb_del_win(char direction);
void kb_del_up(void);
void kb_del_down(void);
void kb_del_left(void);
void kb_del_right(void);

/* Resize the focused window inside its enclosing split.
 * Width: target the nearest left/right (vertical) ancestor split.
 * Height: target the nearest top/bottom (horizontal) ancestor split. */
void kb_win_grow_width(void);
void kb_win_shrink_width(void);
void kb_win_grow_height(void);
void kb_win_shrink_height(void);

void kb_start_insert(void);
void kb_end_append(void);

#endif /* KEYBINDS_BUILTINS_H */
