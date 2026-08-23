#ifndef CMD_EDIT_H
#define CMD_EDIT_H

/* Editing, motion and mode commands backing the default keymaps.
 * Registered by the core plugin; bound via cmap* so every action is
 * discoverable in :keybinds, rebindable, and callable from the :
 * prompt. */

/* Line / char edits */
void cmd_delete_line(const char *args);
void cmd_delete_char(const char *args);
void cmd_delete_eol(const char *args);
void cmd_delete_forward(const char *args);
void cmd_delete_word_left(const char *args);
void cmd_delete_word_right(const char *args);
void cmd_change_line(const char *args);
void cmd_change_eol(const char *args);
void cmd_yank_line(const char *args);
void cmd_join(const char *args);
void cmd_indent(const char *args);
void cmd_unindent(const char *args);
void cmd_move_line_up(const char *args);
void cmd_move_line_down(const char *args);
void cmd_duplicate_line(const char *args);
void cmd_toggle_case(const char *args);
void cmd_toggle_comment(const char *args);
void cmd_replace_char(const char *args);

/* View */
void cmd_center(const char *args);
void cmd_scrollup(const char *args);
void cmd_scrolldown(const char *args);

/* Search / navigation */
void cmd_search(const char *args);
void cmd_search_next(const char *args);
void cmd_search_word(const char *args);
void cmd_search_selection(const char *args);
void cmd_match_bracket(const char *args);
void cmd_jump_back(const char *args);
void cmd_jump_forward(const char *args);
void cmd_openpath(const char *args);
void cmd_searchpath(const char *args);

/* Mode switching */
void cmd_insert(const char *args);
void cmd_append(const char *args);
void cmd_insert_bol(const char *args);
void cmd_append_eol(const char *args);
void cmd_visual(const char *args);
void cmd_visual_line(const char *args);
void cmd_visual_block(const char *args);

/* Operators: act on the visual selection if one is active, on the
 * text object named in args (":delete iw"), or read a 1-2 key text
 * object interactively — same pattern as :goto <motion>. */
void cmd_delete(const char *args);
void cmd_change(const char *args);
void cmd_yank(const char *args);
void cmd_select(const char *args);
void cmd_select_line(const char *args);
void cmd_extend(const char *args);

#endif /* CMD_EDIT_H */
