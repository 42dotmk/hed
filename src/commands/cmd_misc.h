#ifndef CMD_MISC_H
#define CMD_MISC_H

void cmd_list_commands(const char *args);
void cmd_echo(const char *args);
void cmd_history(const char *args);
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
void cmd_ts(const char *args);
void cmd_tslang(const char *args);
void cmd_tsi(const char *args);
void cmd_new_line(const char *args);
void cmd_new_line_above(const char *args);
void cmd_shell(const char *args);
void cmd_git(const char *args);
void cmd_wrap(const char *args);
void cmd_wrapdefault(const char *args);
void cmd_reload(const char *args);
void cmd_fmt(const char *args);
void cmd_tmux_toggle(const char *args);
void cmd_tmux_send(const char *args);
void cmd_tmux_kill(const char *args);
void cmd_tag(const char *args);

#endif /* CMD_MISC_H */
