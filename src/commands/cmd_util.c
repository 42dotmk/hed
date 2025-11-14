#include "cmd_util.h"
#include <stdlib.h>

void shell_escape_single(const char *in, char *out, size_t outsz) {
    size_t o = 0;
    if (o < outsz) out[o++] = '\'';
    for (const char *p = in; *p && o + 4 < outsz; p++) {
        if (*p == '\'') {
            /* Emit '\'' pattern */
            out[o++] = '\''; out[o++] = '\\'; out[o++] = '\''; out[o++] = '\'';
        } else {
            out[o++] = *p;
        }
    }
    if (o < outsz) out[o++] = '\'';
    if (o < outsz) out[o] = '\0'; else out[outsz - 1] = '\0';
}

int parse_int_default(const char *s, int def) {
    if (!s || !*s) return def;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s) return def;
    if (v < 0) v = 0;
    if (v > 100000) v = 100000;
    return (int)v;
}
