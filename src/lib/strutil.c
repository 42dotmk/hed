#include "hed.h"
#include <string.h>
#include <wchar.h>

static int _is_space_char(char c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r');
}

size_t str_trim_whitespace(const char *in, char *out, size_t out_sz) {
    if (!out || out_sz == 0)
        return 0;
    if (!in) {
        out[0] = '\0';
        return 0;
    }
    const char *start = in;
    while (*start && _is_space_char(*start))
        start++;
    const char *end = start + strlen(start);
    while (end > start && _is_space_char(*(end - 1)))
        end--;
    size_t len = (size_t)(end - start);
    if (len >= out_sz)
        len = out_sz - 1;
    if (len > 0)
        memcpy(out, start, len);
    out[len] = '\0';
    return len;
}

size_t str_expand_tilde(const char *in, char *out, size_t out_sz) {
    if (!out || out_sz == 0)
        return 0;
    if (!in) {
        out[0] = '\0';
        return 0;
    }
    if (in[0] != '~') {
        size_t n = strlen(in);
        if (n >= out_sz)
            n = out_sz - 1;
        memcpy(out, in, n);
        out[n] = '\0';
        return n;
    }
    const char *home = getenv("HOME");
    if (!home || !(in[1] == '\0' || in[1] == '/')) {
        /* unsupported ~user or HOME missing: copy as-is */
        size_t n = strlen(in);
        if (n >= out_sz)
            n = out_sz - 1;
        memcpy(out, in, n);
        out[n] = '\0';
        return n;
    }
    /* '~' or '~/' -> expand to $HOME + rest (rest starts at in+1) */
    size_t hlen = strlen(home);
    size_t rest_len = strlen(in + 1); /* includes leading '/' or 0 */
    size_t need = hlen + rest_len;
    if (need >= out_sz)
        need = out_sz - 1;
    size_t copy_home = hlen;
    if (copy_home > need)
        copy_home = need;
    memcpy(out, home, copy_home);
    size_t written = copy_home;
    size_t space_left = out_sz - 1 - written;
    if (space_left > 0 && rest_len > 0) {
        size_t cr = rest_len;
        if (cr > space_left)
            cr = space_left;
        memcpy(out + written, in + 1, cr);
        written += cr;
    }
    out[written] = '\0';
    return written;
}

/* Helper: decode one UTF-8 character and return its byte length.
 * Returns 0 for invalid sequences. */
static size_t utf8_decode_char(const unsigned char *p, size_t remaining,
                               wchar_t *out_wc) {
    if (remaining == 0)
        return 0;

    unsigned char c = p[0];

    /* ASCII: 0xxxxxxx */
    if ((c & 0x80) == 0) {
        *out_wc = (wchar_t)c;
        return 1;
    }

    /* 2-byte: 110xxxxx 10xxxxxx */
    if ((c & 0xE0) == 0xC0) {
        if (remaining < 2 || (p[1] & 0xC0) != 0x80)
            return 0;
        *out_wc = (wchar_t)(((c & 0x1F) << 6) | (p[1] & 0x3F));
        return 2;
    }

    /* 3-byte: 1110xxxx 10xxxxxx 10xxxxxx */
    if ((c & 0xF0) == 0xE0) {
        if (remaining < 3 || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80)
            return 0;
        *out_wc = (wchar_t)(((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) |
                            (p[2] & 0x3F));
        return 3;
    }

    /* 4-byte: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
    if ((c & 0xF8) == 0xF0) {
        if (remaining < 4 || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 ||
            (p[3] & 0xC0) != 0x80)
            return 0;
        *out_wc = (wchar_t)(((c & 0x07) << 18) | ((p[1] & 0x3F) << 12) |
                            ((p[2] & 0x3F) << 6) | (p[3] & 0x3F));
        return 4;
    }

    /* Invalid UTF-8 sequence */
    return 0;
}

int utf8_display_width(const char *str, size_t byte_len) {
    if (!str)
        return 0;

    int total_width = 0;
    const unsigned char *p = (const unsigned char *)str;
    size_t i = 0;

    while (i < byte_len) {
        wchar_t wc;
        size_t char_len = utf8_decode_char(p + i, byte_len - i, &wc);

        if (char_len == 0) {
            /* Invalid UTF-8: treat as 1 column and skip 1 byte */
            total_width += 1;
            i += 1;
        } else {
            /* Valid character: use wcwidth() */
            int w = wcwidth(wc);
            if (w < 0) {
                /* Control character or non-printable: treat as 0 width */
                w = 0;
            }
            total_width += w;
            i += char_len;
        }
    }

    return total_width;
}

void utf8_slice_by_columns(const char *str, size_t byte_len, int start_col,
                           int num_cols, int *out_byte_start,
                           int *out_byte_len) {
    if (!str || !out_byte_start || !out_byte_len) {
        if (out_byte_start)
            *out_byte_start = 0;
        if (out_byte_len)
            *out_byte_len = 0;
        return;
    }

    const unsigned char *p = (const unsigned char *)str;
    size_t i = 0;
    int current_col = 0;
    int slice_start_byte = 0;
    int slice_end_byte = 0;
    int found_start = 0;
    int found_end = 0;

    while (i < byte_len && !found_end) {
        size_t char_start = i;
        wchar_t wc;
        size_t char_len = utf8_decode_char(p + i, byte_len - i, &wc);

        int char_width;
        if (char_len == 0) {
            /* Invalid UTF-8: 1 column, 1 byte */
            char_width = 1;
            char_len = 1;
        } else {
            int w = wcwidth(wc);
            char_width = (w < 0) ? 0 : w;
        }

        /* Check if we've reached the start column */
        if (!found_start && current_col >= start_col) {
            slice_start_byte = (int)char_start;
            found_start = 1;
        }

        /* Check if we've collected enough columns */
        if (found_start && current_col >= start_col + num_cols) {
            slice_end_byte = (int)char_start;
            found_end = 1;
            break;
        }

        current_col += char_width;
        i += char_len;
    }

    /* If we haven't found the end yet, use the end of the string */
    if (found_start && !found_end) {
        slice_end_byte = (int)byte_len;
    }

    /* If we never reached start_col, return empty slice */
    if (!found_start) {
        *out_byte_start = (int)byte_len;
        *out_byte_len = 0;
        return;
    }

    *out_byte_start = slice_start_byte;
    *out_byte_len = slice_end_byte - slice_start_byte;
}
