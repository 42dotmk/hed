#ifndef COMMANDS_H
#define COMMANDS_H

/* Command callback signature */
typedef void (*CommandCallback)(const char *args);

/* Command API */

/**
 * Initialize the command system
 * Called once during editor initialization
 */
void command_init(void);

/**
 * Register a custom command
 *
 * @param name     Command name (without the ':')
 * @param callback Function to call when command is executed
 *
 * Examples:
 *   command_register("save", cmd_save);      // :save
 *   command_register("format", cmd_format);  // :format
 *   command_register("run", cmd_run);        // :run
 */
void command_register(const char *name, CommandCallback callback);

/**
 * Execute a command by name with arguments
 *
 * @param name Command name (without the ':')
 * @param args Arguments string (can be NULL or empty)
 * @return 1 if command was found and executed, 0 if not found
 */
int command_execute(const char *name, const char *args);

/**
 * User commands initialization (implemented in config.c)
 * This is where users define their custom commands
 */
void user_commands_init(void);

void cmd_quit(const char *args);
void cmd_list_commands(const char *args);
void cmd_quit_force(const char *args);
void cmd_write(const char *args);
void cmd_write_quit(const char *args);
void cmd_buffer_next(const char *args);
void cmd_buffer_prev(const char *args);
void cmd_buffer_list(const char *args);
void cmd_buffer_switch(const char *args);
void cmd_buffer_delete(const char *args);
void cmd_edit(const char *args);
#endif
