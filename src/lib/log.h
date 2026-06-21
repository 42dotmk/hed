#ifndef LOG_H
#define LOG_H

#include <stdarg.h>

/* Simple logger that appends to a file (default: .hedlog). */

/* Initialize logging to the given file path. Safe to call multiple times. */
void log_init(const char *path);

/* Append a message (printf-style). Adds a timestamp prefix. */
void log_msg(const char *fmt, ...);

/* Truncate the log file (keeps logging enabled). */
void log_clear(void);

/* Path of the active log file, or "" if logging is uninitialized. */
const char *log_path(void);

/* fd of the active log file, or -1 if logging is uninitialized.
 * Plugins that fork+exec children should dup2() this onto the
 * child's STDERR_FILENO so the child's stderr lands in the log
 * instead of repainting on top of the editor's frames. */
int log_fileno(void);

/* Redirect the calling process's stderr (fd 2) to the log file.
 * Affects every subsequent write to fd 2, including any child that
 * inherits stderr without its own dup2. Use sparingly — most callers
 * want log_fileno() + a per-child dup2 instead. Returns 0 on
 * success, -1 if logging isn't open. */
int log_stderr_to_logfile(void);

/* Close the log file. Called automatically on exit. */
void log_close(void);

#endif /* LOG_H */
