#include "lib/args.h"

#include <ctype.h>
#include <string.h>

const char *args_skip_ws(const char *s) {
    while (s && isspace((unsigned char)*s))
        s++;
    return s;
}

const char *args_next_token(const char *s, char *dst, size_t dst_sz) {
    size_t i = 0;
    s = args_skip_ws(s);
    while (s && *s && !isspace((unsigned char)*s)) {
        if (i + 1 < dst_sz)
            dst[i++] = *s;
        s++;
    }
    if (dst_sz > 0)
        dst[i] = '\0';
    return s;
}

int args_tristate(const char *args, int cur) {
    args = args_skip_ws(args);
    if (!args || !*args || strcmp(args, "toggle") == 0)
        return !cur;
    if (strcmp(args, "on") == 0 || strcmp(args, "1") == 0)
        return 1;
    if (strcmp(args, "off") == 0 || strcmp(args, "0") == 0)
        return 0;
    return -1;
}

int args_dispatch(const char *args, const ArgVerb *verbs, int n) {
    char verb[64];
    const char *rest = args_skip_ws(args_next_token(args, verb, sizeof(verb)));
    for (int i = 0; i < n; i++) {
        if (strcmp(verb, verbs[i].name) == 0) {
            verbs[i].fn(rest);
            return 1;
        }
    }
    return 0;
}
