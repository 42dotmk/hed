#ifndef COMMAND_MODE_H
#define COMMAND_MODE_H

/* Command-line mode processing and completion */

/**
 * Handle keypress in command mode.
 * Processes input for command-line editing, history navigation,
 * and command execution.
 */
void command_mode_handle_keypress(int c);

/**
 * Process and execute the current command in the command buffer.
 * Parses the command name and arguments, executes the command,
 * and handles mode transitions.
 */
void ed_process_command(void);

/**
 * Clear the command-line completion state.
 */
void cmdcomp_clear(void);

/**
 * Cycle to the next completion candidate.
 * If completion is not active, builds the completion list first.
 */
void cmdcomp_next(void);

#endif /* COMMAND_MODE_H */
