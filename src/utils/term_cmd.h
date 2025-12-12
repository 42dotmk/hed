#ifndef TERM_CMD_H
#define TERM_CMD_H

/*
 * TERMINAL COMMAND EXECUTION UTILITY
 * ===================================
 *
 * Provides utilities for running external terminal commands from the editor.
 * Handles raw mode management and output capture.
 *
 * Key Features:
 * - Automatically disables/enables raw mode
 * - Captures command output into string array
 * - Supports interactive commands (fzf, etc.)
 * - Handles stderr and exit codes
 */

/*
 * Run a terminal command and capture output lines.
 *
 * Temporarily disables raw mode, runs the command via popen(),
 * captures all output lines, then re-enables raw mode.
 *
 * Parameters:
 *   cmd        - Shell command to execute
 *   out_lines  - Output: array of strings (one per line), caller must free with
 * term_cmd_free() out_count  - Output: number of lines captured
 *
 * Returns:
 *   1 on success (command ran, may have captured 0+ lines)
 *   0 on failure (popen failed)
 *
 * Example:
 *   char **lines;
 *   int count;
 *   if (term_cmd_run("ls -la", &lines, &count)) {
 *       // Process lines...
 *       term_cmd_free(lines, count);
 *   }
 */
int term_cmd_run(const char *cmd, char ***out_lines, int *out_count);

/* Run a non-interactive command via system(), handling raw mode toggling. */
int term_cmd_system(const char *cmd);

/*
 * Run a terminal command interactively without capturing output.
 *
 * Temporarily disables raw mode, runs the command via system(),
 * then re-enables raw mode. Useful for interactive commands that
 * don't need output capture.
 *
 * Parameters:
 *   cmd - Shell command to execute
 *
 * Returns:
 *   Exit status from system()
 *
 * Example:
 *   term_cmd_run_interactive("make");
 */
int term_cmd_run_interactive(const char *cmd, BOOL acknowledge);

/*
 * Free memory allocated by term_cmd_run().
 *
 * Parameters:
 *   lines - Array of strings from term_cmd_run()
 *   count - Number of strings in array
 */
void term_cmd_free(char **lines, int count);

#endif /* TERM_CMD_H */
