#ifndef UTILS_UNDER_CURSOR_H
#define UTILS_UNDER_CURSOR_H

#include "lib/strbuf.h"

/*
 * Content-under-cursor extractors for the current buffer/window.
 * Return 1 on success, 0 on failure.
 */

/* Borrowed: points into the row buffer, no allocation. Valid only until
 * the buffer is next edited — copy via strbuf_from_view() to keep it. */
int buf_word_view_under_cursor(StrView *out);
int buf_get_word_under_cursor(StrBuf *out);
int buf_get_paragraph_under_cursor(StrBuf *out);
int buf_get_path_under_cursor(StrBuf *out, int *out_line, int *out_col);

#endif
