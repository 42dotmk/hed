#ifndef CMD_MISC_H
#define CMD_MISC_H

void cmd_list_commands(const char *args);
void cmd_list_keybinds(const char *args);
void cmd_echo(const char *args);
void cmd_history(const char *args);
void cmd_history_fzf(const char *args);
void cmd_jumplist_fzf(const char *args);
void cmd_registers(const char *args);
void cmd_put(const char *args);
void cmd_undo(const char *args);
void cmd_redo(const char *args);
void cmd_repeat(const char *args);
void cmd_macro_record(const char *args);
void cmd_macro_play(const char *args);
void cmd_ln(const char *args);
void cmd_rln(const char *args);
void cmd_logclear(const char *args);
void cmd_new_line(const char *args);
void cmd_new_line_above(const char *args);
void cmd_git(const char *args);
void cmd_wrap(const char *args);
void cmd_wrapdefault(const char *args);
void cmd_tag(const char *args);
void cmd_modal_from_current(const char *args);
void cmd_modal_to_layout(const char *args);
void cmd_fold_new(const char *args);
void cmd_fold_rm(const char *args);
void cmd_fold_toggle(const char *args);
void cmd_foldmethod(const char *args);
void cmd_foldupdate(const char *args);
void cmd_buf_refresh(const char *args);

#endif /* CMD_MISC_H */
