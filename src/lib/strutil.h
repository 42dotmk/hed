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

/* UTF-8 display width calculation using wcwidth().
 * Returns the display width in columns for a UTF-8 string.
 * Handles wide characters (CJK, emoji) correctly.
 * Invalid UTF-8 sequences are counted as 1 column each. */
int utf8_display_width(const char *str, size_t byte_len);

/* Extract a substring by column positions (not byte positions).
 * str: input UTF-8 string
 * byte_len: length of input in bytes
 * start_col: starting column (0-based)
 * num_cols: number of columns to extract
 * out_byte_start: [output] byte offset where slice starts
 * out_byte_len: [output] length of slice in bytes
 *
 * Example: "Hello你好" (5 ASCII + 2 wide chars = 9 columns)
 *   start_col=5, num_cols=4 -> extracts "你好" (6 bytes)
 */
void utf8_slice_by_columns(const char *str, size_t byte_len,
                           int start_col, int num_cols,
                           int *out_byte_start, int *out_byte_len);

#endif
