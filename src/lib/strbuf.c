#include "lib/strbuf.h"
#include <stdlib.h>
#include <string.h>

StrBuf strbuf_new(void) {
    StrBuf s = {NULL, 0, 0};
    return s;
}

StrBuf strbuf_from(const char *data, size_t len) {
    StrBuf s = {NULL, 0, 0};
    if (len > 0) {
        s.data = malloc(len + 1);
        if (!s.data)
            return s; /* Return empty on OOM */
        memcpy(s.data, data, len);
        s.data[len] = '\0';
        s.len = len;
        s.cap = len + 1;
    }
    return s;
}

StrBuf strbuf_from_cstr(const char *cstr) {
    return strbuf_from(cstr, strlen(cstr));
}

void strbuf_free(StrBuf *s) {
    if (s->data) {
        free(s->data);
        s->data = NULL;
    }
    s->len = 0;
    s->cap = 0;
}

void strbuf_clear(StrBuf *s) {
    s->len = 0;
    if (s->data) {
        s->data[0] = '\0';
    }
}

void strbuf_reserve(StrBuf *s, size_t capacity) {
    if (capacity > s->cap) {
        char *new_data = realloc(s->data, capacity);
        if (!new_data)
            return; /* Keep old data on OOM */
        s->data = new_data;
        s->cap = capacity;
        if (s->len == 0 && s->data) {
            s->data[0] = '\0';
        }
    }
}

void strbuf_append_char(StrBuf *s, int c) {
    if (s->len + 2 > s->cap) {
        strbuf_reserve(s, s->cap == 0 ? 32 : s->cap * 2);
    }
    s->data[s->len++] = c;
    s->data[s->len] = '\0';
}

void strbuf_append(StrBuf *s, const char *data, size_t len) {
    if (len == 0)
        return;
    if (s->len + len + 1 > s->cap) {
        /* Grow geometrically so repeated appends stay amortised O(1) —
         * important for streaming/byte-at-a-time builders. */
        size_t want = s->cap ? s->cap : 32;
        while (want < s->len + len + 1)
            want *= 2;
        strbuf_reserve(s, want);
    }
    memcpy(s->data + s->len, data, len);
    s->len += len;
    s->data[s->len] = '\0';
}

void strbuf_insert_char(StrBuf *s, size_t pos, int c) {
    if (pos > s->len)
        pos = s->len;
    if (s->len + 2 > s->cap) {
        strbuf_reserve(s, s->cap == 0 ? 32 : s->cap * 2);
    }
    memmove(s->data + pos + 1, s->data + pos, s->len - pos + 1);
    s->data[pos] = c;
    s->len++;
}

void strbuf_delete_char(StrBuf *s, size_t pos) {
    if (pos >= s->len)
        return;
    memmove(s->data + pos, s->data + pos + 1, s->len - pos);
    s->len--;
}

char *strbuf_to_cstr(const StrBuf *s) {
    if (!s->data)
        return NULL;
    char *result = malloc(s->len + 1);
    if (!result)
        return NULL; /* Return NULL on OOM */
    memcpy(result, s->data, s->len);
    result[s->len] = '\0';
    return result;
}

void strbuf_append_shell_quoted_n(StrBuf *s, const char *in, size_t n) {
    strbuf_append_char(s, '\'');
    for (size_t i = 0; i < n; i++) {
        if (in[i] == '\'')
            strbuf_append(s, "'\\''", 4);
        else
            strbuf_append_char(s, in[i]);
    }
    strbuf_append_char(s, '\'');
}

void strbuf_append_shell_quoted(StrBuf *s, const char *in) {
    strbuf_append_shell_quoted_n(s, in, in ? strlen(in) : 0);
}

/* ---- StrView (borrowed) helpers ---- */

StrView strview(const char *data, size_t len) {
    StrView v = {data, len};
    return v;
}

StrView strview_from_cstr(const char *s) {
    return strview(s, s ? strlen(s) : 0);
}

StrView strbuf_view(const StrBuf *s) {
    return strview(s->data, s->len);
}

int strview_eq(StrView a, StrView b) {
    return a.len == b.len &&
           (a.len == 0 || memcmp(a.data, b.data, a.len) == 0);
}

StrBuf strbuf_from_view(StrView v) {
    return strbuf_from(v.data, v.len);
}

void strbuf_append_view(StrBuf *s, StrView v) {
    strbuf_append(s, v.data, v.len);
}
