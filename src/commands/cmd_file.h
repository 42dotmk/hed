#ifndef CMD_FILE_H
#define CMD_FILE_H

/*
 * FILE OPERATION COMMAND MODULE
 * ==============================
 *
 * Commands for file I/O and editor lifecycle.
 *
 * Commands:
 * - :q, :quit      - Quit editor (with unsaved check)
 * - :q!, :quit!    - Force quit without saving
 * - :w [file]      - Write buffer to file
 * - :wq            - Write and quit
 * - :e <file>      - Edit/open file
 * - :cd [path]     - Change/show working directory
 */

void cmd_quit(const char *args);
void cmd_quit_force(const char *args);
void cmd_write(const char *args);
void cmd_write_quit(const char *args);
void cmd_edit(const char *args);
void cmd_cd(const char *args);

#endif /* CMD_FILE_H */
