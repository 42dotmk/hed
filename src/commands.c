#include "commands.h"
#include <stdlib.h>
#include <string.h>

#define MAX_COMMANDS 256

/* Global command storage - exposed for cmd_search and cmd_misc */
Command commands[MAX_COMMANDS];
int command_count = 0;

/* Initialize command system */
void command_init(void) {
    command_count = 0;
    user_commands_init();
}

/* Register a command */
void command_register(const char *name, CommandCallback callback,  const char *desc) {
    if (command_count >= MAX_COMMANDS)
        return;

    char *name_copy = strdup(name);
    if (!name_copy)
        return; /* OOM: fail gracefully */

    char *desc_copy = NULL;
    if (desc) {
        desc_copy = strdup(desc);
        if (!desc_copy) {
            free(name_copy);
            return; /* OOM: cleanup and fail */
        }
    }

    commands[command_count].name = name_copy;
    commands[command_count].callback = callback;
    commands[command_count].desc = desc_copy;
    command_count++;
}

/* Execute a command by name */
int command_execute(const char *name, const char *args) {
    for (int i = 0; i < command_count; i++) {
        if (commands[i].name && strcmp(commands[i].name, name) == 0) {
            if (commands[i].callback) {
                commands[i].callback(args);
                return 1;
            }
        }
    }
    return 0;
}

/* Helper to invoke a command programmatically */
int command_invoke(const char *name, const char *args) {
    return command_execute(name, args);
}
