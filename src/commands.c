#include "commands.h"
#include "stb_ds.h"
#include <stdlib.h>
#include <string.h>

/* Global command storage - exposed for cmd_search and cmd_misc */
Command *commands = NULL;

void command_init(void) {
    /* arrfree(NULL) is a no-op; safe even on first call. */
    arrfree(commands);
    commands = NULL;
}

void command_register(const char *name, CommandCallback callback,
                      const char *desc) {
    char *name_copy = strdup(name);
    if (!name_copy)
        return;

    char *desc_copy = NULL;
    if (desc) {
        desc_copy = strdup(desc);
        if (!desc_copy) {
            free(name_copy);
            return;
        }
    }

    Command cmd = {.name = name_copy, .callback = callback, .desc = desc_copy};
    arrput(commands, cmd);
}

int command_execute(const char *name, const char *args) {
    for (ptrdiff_t i = 0; i < arrlen(commands); i++) {
        if (commands[i].name && strcmp(commands[i].name, name) == 0) {
            if (commands[i].callback) {
                commands[i].callback(args);
                return 1;
            }
        }
    }
    return 0;
}

int command_invoke(const char *name, const char *args) {
    return command_execute(name, args);
}

const char *command_find_desc(const char *name) {
    if (!name) return NULL;
    for (ptrdiff_t i = 0; i < arrlen(commands); i++) {
        if (commands[i].name && strcmp(commands[i].name, name) == 0) {
            return commands[i].desc;
        }
    }
    return NULL;
}
