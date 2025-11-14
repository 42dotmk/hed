#ifndef STRUTIL_H
#define STRUTIL_H

#include <stddef.h>

/* Copy input string with leading/trailing ASCII whitespace removed.
 * Returns the number of bytes written to out (excluding NUL). */
size_t str_trim_whitespace(const char *in, char *out, size_t out_sz);

/* Expand leading '~' to $HOME when the input begins with '~' or '~/'
 * Writes into out; returns number of bytes written (excluding NUL).
 * If no expansion performed (or HOME unset), copies input as-is. */
size_t str_expand_tilde(const char *in, char *out, size_t out_sz);

#endif
