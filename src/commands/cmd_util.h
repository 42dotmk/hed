#ifndef CMD_UTIL_H
#define CMD_UTIL_H

#include <stddef.h>

/*
 * COMMAND UTILITIES
 * =================
 *
 * Shared helper functions for command implementations.
 */

/*
 * Escape string for safe use in shell single-quoted context.
 *
 * In POSIX shells, single quotes protect all characters literally except
 * the single quote itself. To include a literal single quote within a
 * single-quoted string, we use the pattern: '\''
 *
 * This works by:
 *   1. End the current single-quoted string (')
 *   2. Add an escaped single quote (\')
 *   3. Start a new single-quoted string (')
 *
 * Example: "it's" becomes 'it'\''s' which shells interpret as: it's
 *
 * This approach is secure because:
 *   - All shell metacharacters ($, `, \, etc.) are literal within single quotes
 *   - The only special character (') is explicitly handled
 *   - No shell expansion or interpretation occurs
 *
 * @param in Input string to escape
 * @param out Output buffer for escaped string
 * @param outsz Size of output buffer
 */
void shell_escape_single(const char *in, char *out, size_t outsz);

/*
 * Parse integer from string with default value.
 * Returns default_val if string is NULL, empty, or invalid.
 */
int parse_int_default(const char *str, int default_val);

#endif /* CMD_UTIL_H */
