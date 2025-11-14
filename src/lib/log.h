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

/* Close the log file. Called automatically on exit. */
void log_close(void);

#endif /* LOG_H */

