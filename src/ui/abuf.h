#ifndef ABUF_H
#define ABUF_H

#include <stddef.h>

typedef struct Abuf {
    char *data;
    int len;
    int cap;
} Abuf;

void ab_init(Abuf *ab);
void ab_free(Abuf *ab);
void ab_append(Abuf *ab, const void *src, int n);
void ab_append_str(Abuf *ab, const char *s);

#endif /* ABUF_H */
