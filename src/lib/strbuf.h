#ifndef STRBUF_H
#define STRBUF_H

#include <stddef.h>

/* StrBuf: an owned, growable, always-NUL-terminated string buffer. */
typedef struct {
    char *data;
    size_t len;
    size_t cap; /* allocated bytes, including the trailing NUL */
} StrBuf;

/* StrBuf helper functions */
StrBuf strbuf_new(void);
StrBuf strbuf_from(const char *s, size_t len);
StrBuf strbuf_from_cstr(const char *s);
void strbuf_free(StrBuf *s);
void strbuf_clear(StrBuf *s);
void strbuf_reserve(StrBuf *s, size_t capacity);
void strbuf_append_char(StrBuf *s, int c);
void strbuf_append(StrBuf *s, const char *data, size_t len);
void strbuf_insert_char(StrBuf *s, size_t pos, int c);
void strbuf_delete_char(StrBuf *s, size_t pos);
char *strbuf_to_cstr(const StrBuf *s);

/* Append `in` wrapped in POSIX single quotes, escaping embedded single
 * quotes via the '\'' pattern. The growable analogue of
 * shell_escape_single() for callers building unbounded commands. */
void strbuf_append_shell_quoted(StrBuf *s, const char *in);

#endif
