#include "hed.h"

#define MAX_COMMANDS 256

/* Command entry */
typedef struct {
    char *name;
    CommandCallback callback;
} Command;

/* Global command storage */
static Command commands[MAX_COMMANDS];
static int command_count = 0;

/*** Default command callbacks ***/

void cmd_quit(const char *args) {
    (void)args;
    Buffer *buf = buf_cur();
    if (buf && buf->dirty) {
        ed_set_status_message("File has unsaved changes! Use :q! to force quit");
    } else {
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
    }
}

void cmd_list_commands(const char *args) {
    (void)args;
    char msg[256] = "Commands: ";
    for (int i = 0; i < command_count; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%s ", commands[i].name);
        strncat(msg, buf, sizeof(msg) - strlen(msg) - 1);
    }
    ed_set_status_message("%s", msg);
}
void cmd_quit_force(const char *args) {
    (void)args;
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
}

void cmd_write(const char *args) {
    (void)args;
    buf_save();
}

void cmd_write_quit(const char *args) {
    (void)args;
    buf_save();
    Buffer *buf = buf_cur();
    if (buf && !buf->dirty) {
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
    }
}

void cmd_buffer_next(const char *args) {
    (void)args;
    buf_next();
}

void cmd_buffer_prev(const char *args) {
    (void)args;
    buf_prev();
}

void cmd_buffer_list(const char *args) {
    (void)args;
    buf_list();
}

void cmd_buffer_switch(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("Usage: :b <buffer_number>");
        return;
    }
    int buf_idx = atoi(args) - 1;
    buf_switch(buf_idx);
}

void cmd_buffer_delete(const char *args) {
    if (!args || !*args) {
        buf_close(E.current_buffer);
    } else {
        int buf_idx = atoi(args) - 1;
        buf_close(buf_idx);
    }
}

void cmd_edit(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("Usage: :e <filename>");
        return;
    }
    buf_open((char *)args);
}

/* Register default commands */
static void default_commands_init(void) {
    command_register("q", cmd_quit);
    command_register("q!", cmd_quit_force);
    command_register("quit", cmd_quit);
    command_register("w", cmd_write);
    command_register("wq", cmd_write_quit);
    command_register("bn", cmd_buffer_next);
    command_register("bp", cmd_buffer_prev);
    command_register("ls", cmd_buffer_list);
    command_register("b", cmd_buffer_switch);
    command_register("bd", cmd_buffer_delete);
    command_register("e", cmd_edit);
    command_register("c", cmd_list_commands);
}

/* Initialize command system */
void command_init(void) {
    command_count = 0;

    /* Register default commands first */
    default_commands_init();

    /* Then call user commands initialization (can override defaults) */
    user_commands_init();
}

/* Register a command */
void command_register(const char *name, CommandCallback callback) {
    if (command_count >= MAX_COMMANDS) {
        return;
    }

    commands[command_count].name = strdup(name);
    commands[command_count].callback = callback;
    command_count++;
}

/* Execute a command by name */
int command_execute(const char *name, const char *args) {
    for (int i = 0; i < command_count; i++) {
        if (strcmp(commands[i].name, name) == 0) {
            commands[i].callback(args);
            return 1;
        }
    }
    return 0;
}
