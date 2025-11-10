#include "hed.h"
#include "abuf.h"

static void ab_reserve(Abuf *ab, int add) {
    if (add <= 0) return;
    if (ab->len + add <= ab->cap) return;
    int newcap = ab->cap ? ab->cap : 4096;
    while (newcap < ab->len + add) newcap *= 2;
    ab->data = realloc(ab->data, (size_t)newcap);
    ab->cap = newcap;
}

void ab_init(Abuf *ab) {
    ab->data = NULL;
    ab->len = 0;
    ab->cap = 0;
}

void ab_free(Abuf *ab) {
    if (!ab) return;
    free(ab->data);
    ab->data = NULL;
    ab->len = ab->cap = 0;
}

void ab_append(Abuf *ab, const void *src, int n) {
    if (n <= 0) return;
    ab_reserve(ab, n);
    memcpy(&ab->data[ab->len], src, (size_t)n);
    ab->len += n;
}

void ab_append_str(Abuf *ab, const char *s) {
    if (!s) return;
    ab_append(ab, s, (int)strlen(s));
}

