#include "hed.h"

static int _is_space_char(char c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r');
}

size_t str_trim_whitespace(const char *in, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return 0;
    if (!in) { out[0] = '\0'; return 0; }
    const char *start = in;
    while (*start && _is_space_char(*start)) start++;
    const char *end = start + strlen(start);
    while (end > start && _is_space_char(*(end - 1))) end--;
    size_t len = (size_t)(end - start);
    if (len >= out_sz) len = out_sz - 1;
    if (len > 0) memcpy(out, start, len);
    out[len] = '\0';
    return len;
}

size_t str_expand_tilde(const char *in, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return 0;
    if (!in) { out[0] = '\0'; return 0; }
    if (in[0] != '~') {
        size_t n = strlen(in);
        if (n >= out_sz) n = out_sz - 1;
        memcpy(out, in, n); out[n] = '\0';
        return n;
    }
    const char *home = getenv("HOME");
    if (!home || !(in[1] == '\0' || in[1] == '/')) {
        /* unsupported ~user or HOME missing: copy as-is */
        size_t n = strlen(in);
        if (n >= out_sz) n = out_sz - 1;
        memcpy(out, in, n); out[n] = '\0';
        return n;
    }
    /* '~' or '~/' -> expand to $HOME + rest (rest starts at in+1) */
    size_t hlen = strlen(home);
    size_t rest_len = strlen(in + 1); /* includes leading '/' or 0 */
    size_t need = hlen + rest_len;
    if (need >= out_sz) need = out_sz - 1;
    size_t copy_home = hlen; if (copy_home > need) copy_home = need;
    memcpy(out, home, copy_home);
    size_t written = copy_home;
    size_t space_left = out_sz - 1 - written;
    if (space_left > 0 && rest_len > 0) {
        size_t cr = rest_len; if (cr > space_left) cr = space_left;
        memcpy(out + written, in + 1, cr);
        written += cr;
    }
    out[written] = '\0';
    return written;
}
