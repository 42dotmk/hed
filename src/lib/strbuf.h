#ifndef STRBUF_H
#define STRBUF_H

#include <stddef.h>

/* StrBuf: an owned, growable, always-NUL-terminated string buffer. */
typedef struct {
    char *data;
    size_t len;
    size_t cap; /* allocated bytes, including the trailing NUL */
} StrBuf;

/* StrView: a borrowed, read-only slice of bytes. Does NOT own its data
 * and is NOT guaranteed NUL-terminated. Never free a StrView, and never
 * hold one past the lifetime of the storage it points into (e.g. a row
 * buffer is invalidated by the next edit). Pass it by value. */
typedef struct {
    const char *data;
    size_t len;
} StrView;

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
 * shell_escape_single() for callers building unbounded commands.
 * The _n form quotes exactly `n` bytes (allowing embedded NULs / slices);
 * the cstr form quotes up to the NUL (NULL is treated as empty). */
void strbuf_append_shell_quoted_n(StrBuf *s, const char *in, size_t n);
void strbuf_append_shell_quoted(StrBuf *s, const char *in);

/* ---- StrView (borrowed) helpers ---- */

StrView strview(const char *data, size_t len);
StrView strview_from_cstr(const char *s);
StrView strbuf_view(const StrBuf *s);          /* borrow a StrBuf, no copy */
int     strview_eq(StrView a, StrView b);

/* Bridges from a borrowed view back to owned StrBuf storage (these copy). */
StrBuf strbuf_from_view(StrView v);
void   strbuf_append_view(StrBuf *s, StrView v);

#endif
