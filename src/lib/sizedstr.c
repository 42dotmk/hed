#include "hed.h"

SizedStr sstr_new(void) {
    SizedStr s = {NULL, 0, 0};
    return s;
}

SizedStr sstr_from(const char *data, size_t len) {
    SizedStr s = {NULL, 0, 0};
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

SizedStr sstr_from_cstr(const char *cstr) {
    return sstr_from(cstr, strlen(cstr));
}

void sstr_free(SizedStr *s) {
    if (s->data) {
        free(s->data);
        s->data = NULL;
    }
    s->len = 0;
    s->cap = 0;
}

void sstr_clear(SizedStr *s) {
    s->len = 0;
    if (s->data) {
        s->data[0] = '\0';
    }
}

void sstr_reserve(SizedStr *s, size_t capacity) {
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

void sstr_append_char(SizedStr *s, int c) {
    if (s->len + 2 > s->cap) {
        sstr_reserve(s, s->cap == 0 ? 32 : s->cap * 2);
    }
    s->data[s->len++] = c;
    s->data[s->len] = '\0';
}

void sstr_append(SizedStr *s, const char *data, size_t len) {
    if (len == 0)
        return;
    if (s->len + len + 1 > s->cap) {
        sstr_reserve(s, s->len + len + 1);
    }
    memcpy(s->data + s->len, data, len);
    s->len += len;
    s->data[s->len] = '\0';
}

void sstr_insert_char(SizedStr *s, size_t pos, int c) {
    if (pos > s->len)
        pos = s->len;
    if (s->len + 2 > s->cap) {
        sstr_reserve(s, s->cap == 0 ? 32 : s->cap * 2);
    }
    memmove(s->data + pos + 1, s->data + pos, s->len - pos + 1);
    s->data[pos] = c;
    s->len++;
}

void sstr_delete_char(SizedStr *s, size_t pos) {
    if (pos >= s->len)
        return;
    memmove(s->data + pos, s->data + pos + 1, s->len - pos);
    s->len--;
}

char *sstr_to_cstr(const SizedStr *s) {
    if (!s->data)
        return NULL;
    char *result = malloc(s->len + 1);
    if (!result)
        return NULL; /* Return NULL on OOM */
    memcpy(result, s->data, s->len);
    result[s->len] = '\0';
    return result;
}
