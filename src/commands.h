#ifndef COMMANDS_H
#define COMMANDS_H

/* Command callback signature */
typedef void (*CommandCallback)(const char *args);

/* Command entry structure */
typedef struct {
    char *name;
    CommandCallback callback;
    char *desc;
} Command;

/* Built-in command declarations live in commands/cmd_builtins.h */

/* Global command storage (for internal use by command modules) */
extern Command commands[];
extern int command_count;

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
void command_register(const char *name, CommandCallback callback,
                      const char *desc);

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

/* Helper to invoke a command programmatically (e.g., from keymaps) */
int command_invoke(const char *name, const char *args);

#endif
