#ifndef CMD_BUFFER_H
#define CMD_BUFFER_H

/*
 * BUFFER COMMAND MODULE
 * =====================
 *
 * Commands for buffer navigation and management.
 *
 * Commands:
 * - :bnext, :bn    - Switch to next buffer
 * - :bprev, :bp    - Switch to previous buffer
 * - :blist, :bl    - List all buffers with fzf selection
 * - :b <N>         - Switch to buffer N
 * - :bd [N]        - Delete current buffer or buffer N
 * - :buffers       - Enhanced buffer list with metadata
 */

void cmd_buffer_next(const char *args);
void cmd_buffer_prev(const char *args);
void cmd_buffer_list(const char *args);
void cmd_buffer_switch(const char *args);
void cmd_buffer_delete(const char *args);
void cmd_buffers(const char *args);

#endif /* CMD_BUFFER_H */
