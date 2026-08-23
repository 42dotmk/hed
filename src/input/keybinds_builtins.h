#ifndef KEYBINDS_BUILTINS_H
#define KEYBINDS_BUILTINS_H

/*
 * BUILT-IN KEYBIND CALLBACKS
 * ==========================
 *
 * The shared C-callback surface behind the default keymaps. Most
 * former callbacks here are now :commands (see commands/cmd_edit.h);
 * what remains is either bound directly (movement, insert-mode keys,
 * selection helpers) or used as plugin API (visual selection access,
 * clipboard's yank wrappers).
 */

#include "buf/textobj.h"
#include "ui/window.h"

void kb_enter_command_mode(void);
void kb_yank_line(void);
/* Yank operator (reads a text object); wraps the :yank command so
 * plugins (clipboard) can compose with it. */
void kb_operator_yank(void);
void kb_operator_move(int key); /* Move cursor via text object (fallback) */
/* Save the current position to the jump list (within-file jumps). */
void kb_jump_save_current(void);
/* End-of-line motion used by :select_line. */
void kb_goto_line_end(void);

/* True while any visual mode is active. */
int kb_in_visual(void);

/* Register the modeless basics shared by the emacs and vscode keymaps
 * (Esc/CR/Tab/BS, arrow drop/extend, word-wise and Home/End extend). */
void keybind_register_modeless_basics(void);

void kb_insert_newline(void);
void kb_insert_tab(void);
void kb_insert_backspace(void);
void kb_insert_escape(void);

void kb_visual_yank_selection(void);
void kb_visual_paste(void);

/* Vim f/F/t/T: interactive to-char motions (read the target char from
 * the keyboard, then defer to textobj_to_char). Registered as textobjs
 * so plain use, visual extension and operators (dfx, ctx) all work.
 * ; and , repeat the last one, same or reversed direction. */
int kb_textobj_find_char_fwd(Buffer *buf, int line, int col,
                             TextSelection *sel);
int kb_textobj_find_char_back(Buffer *buf, int line, int col,
                              TextSelection *sel);
int kb_textobj_till_char_fwd(Buffer *buf, int line, int col,
                             TextSelection *sel);
int kb_textobj_till_char_back(Buffer *buf, int line, int col,
                              TextSelection *sel);
int kb_textobj_find_repeat(Buffer *buf, int line, int col, TextSelection *sel);
int kb_textobj_find_repeat_rev(Buffer *buf, int line, int col,
                               TextSelection *sel);
void kb_visual_delete_selection(void);
void kb_visual_escape(void);
void kb_visual_clear(Window *win);
void kb_visual_begin(int block_mode);
int kb_visual_yank(Buffer *buf, Window *win, int block_mode);
int kb_visual_delete(Buffer *buf, Window *win, int block_mode);
/* Build a TextSelection from the window's current visual selection.
 * `block_mode` is forced on if win->sel.type is SEL_VISUAL_BLOCK.
 * Returns 1 on success and fills *out; 0 if there is no selection. */
int kb_visual_to_textsel(Buffer *buf, Window *win, int block_mode,
                         TextSelection *out);

/* Replace char under cursor / every char of the selection with c.
 * The interactive read lives in :replace_char (cmd_edit.c). */
void kb_replace_char_apply(int c);
void kb_visual_replace_char_apply(int c);

void kb_del_win(char direction);

#endif /* KEYBINDS_BUILTINS_H */
