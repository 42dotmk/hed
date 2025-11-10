#ifndef SIZEDSTR_H
#define SIZEDSTR_H

#include <stddef.h>

/* Sized string structure */
typedef struct {
    char *data;
    size_t len;
    size_t cap;  /* capacity for future use */
} SizedStr;

/* SizedStr helper functions */
SizedStr sstr_new(void);
SizedStr sstr_from(const char *s, size_t len);
SizedStr sstr_from_cstr(const char *s);
void sstr_free(SizedStr *s);
void sstr_clear(SizedStr *s);
void sstr_reserve(SizedStr *s, size_t capacity);
void sstr_append_char(SizedStr *s, int c);
void sstr_append(SizedStr *s, const char *data, size_t len);
void sstr_insert_char(SizedStr *s, size_t pos, int c);
void sstr_delete_char(SizedStr *s, size_t pos);
char *sstr_to_cstr(const SizedStr *s);

#endif
