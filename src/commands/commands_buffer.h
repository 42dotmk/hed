#ifndef COMMANDS_BUFFER_H
#define COMMANDS_BUFFER_H

/* Buffer navigation and management */
void cmd_buffer_next(const char *args);
void cmd_buffer_prev(const char *args);
void cmd_buffer_list(const char *args);
void cmd_buffer_switch(const char *args);
void cmd_buffer_delete(const char *args);
void cmd_buffers(const char *args);

/* File I/O and lifecycle */
void cmd_quit(const char *args);
void cmd_quit_force(const char *args);
void cmd_write(const char *args);
void cmd_write_quit(const char *args);
void cmd_edit(const char *args);
void cmd_cd(const char *args);

#endif /* COMMANDS_BUFFER_H */
