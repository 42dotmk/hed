#include "commands/registry.h"
#include "buf/buf_helpers.h"
#include "buf/buffer.h"
#include "stb_ds.h"
#include "utils/under_cursor.h"
#include <stdlib.h>
#include <string.h>

/* Global command storage - exposed so callers can iterate registrations
 * (e.g. the pickers plugin's :c command palette). */
Command *commands = NULL;

void command_init(void) {
    /* arrfree(NULL) is a no-op; safe even on first call. */
    arrfree(commands);
    commands = NULL;
}

void command_register_ft(const char *name, const char *filetype,
                         CommandCallback callback, const char *desc) {
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

    Command cmd = {.name = name_copy,
                   .callback = callback,
                   .desc = desc_copy,
                   .filetype = filetype ? strdup(filetype) : NULL};
    arrput(commands, cmd);
}

void command_register(const char *name, CommandCallback callback,
                      const char *desc) {
    command_register_ft(name, NULL, callback, desc);
}

int command_execute(const char *name, const char *args) {
    Buffer *buf = buf_cur();
    const char *ft = (buf && buf->filetype) ? buf->filetype : NULL;
    CommandCallback global = NULL;
    for (ptrdiff_t i = 0; i < arrlen(commands); i++) {
        if (!commands[i].name || strcmp(commands[i].name, name) != 0 ||
            !commands[i].callback)
            continue;
        if (commands[i].filetype) {
            if (ft && strcmp(commands[i].filetype, ft) == 0) {
                commands[i].callback(args);
                return 1;
            }
        } else if (!global) {
            global = commands[i].callback;
        }
    }
    if (global) {
        global(args);
        return 1;
    }
    return 0;
}

int command_visible(const Command *c) {
    if (!c)
        return 0;
    if (!c->filetype)
        return 1;
    Buffer *buf = buf_cur();
    return buf && buf->filetype && strcmp(buf->filetype, c->filetype) == 0;
}

int command_execute_line(const char *line) {
    if (!line)
        return 0;
    while (*line == ' ' || *line == '\t' || *line == ':')
        line++;
    if (!*line)
        return 0;

    char name[128];
    size_t ni = 0;
    while (*line && *line != ' ' && *line != '\t' && ni + 1 < sizeof(name))
        name[ni++] = *line++;
    name[ni] = '\0';
    while (*line == ' ' || *line == '\t')
        line++;

    /* Token expansion in the args: %w -> the word under the cursor,
     * %% -> a literal %. Lets bindings pass editor context to any
     * command ("man %w", "rg %w") without a C trampoline. */
    if (strchr(line, '%')) {
        char ex[1024];
        size_t o = 0;
        for (const char *p = line; *p && o + 1 < sizeof(ex); p++) {
            if (p[0] == '%' && p[1] == 'w') {
                StrView w;
                if (buf_word_view_under_cursor(&w)) {
                    size_t n = w.len;
                    if (n > sizeof(ex) - 1 - o)
                        n = sizeof(ex) - 1 - o;
                    memcpy(ex + o, w.data, n);
                    o += n;
                }
                p++;
            } else if (p[0] == '%' && p[1] == '%') {
                ex[o++] = '%';
                p++;
            } else {
                ex[o++] = *p;
            }
        }
        ex[o] = '\0';
        return command_execute(name, o ? ex : NULL);
    }

    return command_execute(name, *line ? line : NULL);
}

int command_invoke(const char *name, const char *args) {
    return command_execute(name, args);
}

const char *command_find_desc(const char *name) {
    if (!name)
        return NULL;
    for (ptrdiff_t i = 0; i < arrlen(commands); i++) {
        if (commands[i].name && strcmp(commands[i].name, name) == 0) {
            return commands[i].desc;
        }
    }
    return NULL;
}
