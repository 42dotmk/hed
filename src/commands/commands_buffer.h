#ifndef COMMANDS_BUFFER_H
#define COMMANDS_BUFFER_H

/*
 * BUFFER AND FILE COMMAND MODULE
 * ===============================
 *
 * Consolidated buffer management and file I/O commands.
 * Combines cmd_buffer.h and cmd_file.h.
 *
 * Buffer Navigation Commands:
 * - :bnext, :bn    - Switch to next buffer
 * - :bprev, :bp    - Switch to previous buffer
 * - :blist, :bl    - List all buffers with fzf selection
 * - :b <N>         - Switch to buffer N
 * - :bd [N]        - Delete current buffer or buffer N
 * - :buffers       - Enhanced buffer list with metadata
 *
 * File I/O Commands:
 * - :q, :quit      - Quit editor (with unsaved check)
 * - :q!, :quit!    - Force quit without saving
 * - :w [file]      - Write buffer to file
 * - :wq            - Write and quit
 * - :e <file>      - Edit/open file
 * - :cd [path]     - Change/show working directory
 */

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
