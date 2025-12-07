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
void command_register(const char *name, CommandCallback callback, const char *desc);

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
void cmd_echo(const char *args);
void cmd_history(const char *args);
void cmd_registers(const char *args);
void cmd_put(const char *args);
void cmd_undo(const char *args);
void cmd_redo(const char *args);
void cmd_ln(const char *args);
void cmd_rln(const char *args);

/* ripgrep integration */
void cmd_rg(const char *args);
void cmd_fzf(const char *args);
void cmd_recent(const char *args);
/* Current file incremental search via ripgrep + fzf */
void cmd_ssearch(const char *args);
/* Shell  quickfix */
void cmd_shq(const char *args);
void cmd_shell(const char *args);
void cmd_cd(const char *args);
/* Command picker via fzf */
void cmd_cpick(const char *args);
/* Git (lazygit wrapper) */
void cmd_git(const char *args);
/* Soft-wrap control */
void cmd_wrap(const char *args);
void cmd_wrapdefault(const char *args);

/* Quickfix commands */
void cmd_copen(const char *args);
void cmd_cclose(const char *args);
void cmd_ctoggle(const char *args);
void cmd_cadd(const char *args);
void cmd_cclear(const char *args);
void cmd_cnext(const char *args);
void cmd_cprev(const char *args);
void cmd_copenidx(const char *args);
void cmd_buffers(const char *args);
/* Logging */
void cmd_logclear(const char *args);
/* Window management */
void cmd_split(const char *args);
void cmd_vsplit(const char *args);
void cmd_wfocus(const char *args);
void cmd_wclose(const char *args);
void cmd_new(const char *args);
void cmd_wleft(const char *args);
void cmd_wright(const char *args);
void cmd_wup(const char *args);
void cmd_wdown(const char *args);
/* Syntax highlighting (tree-sitter) */
void cmd_ts(const char *args);      /* :ts on|off|auto */
void cmd_tslang(const char *args);  /* :tslang <name> */
void cmd_tsi(const char *args);     /* :tsi <lang> */
/* Editing convenience */
void cmd_new_line(const char *args);
void cmd_new_line_above(const char *args);
/* Hot reload */
void cmd_reload(const char *args);

#endif
