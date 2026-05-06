#include "macros.h"
#include "editor.h"
#include "registers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern Ed E;

void macro_init(void) {
    E.macro_queue.buffer = NULL;
    E.macro_queue.capacity = 0;
    E.macro_queue.length = 0;
    E.macro_queue.position = 0;

    E.macro_recording.recording = 0;
    E.macro_recording.register_name = '\0';
    E.macro_recording.last_played = '\0';
}

void macro_free(void) {
    if (E.macro_queue.buffer) {
        free(E.macro_queue.buffer);
        E.macro_queue.buffer = NULL;
    }
    E.macro_queue.capacity = 0;
    E.macro_queue.length = 0;
    E.macro_queue.position = 0;
}

void macro_queue_clear(void) {
    E.macro_queue.length = 0;
    E.macro_queue.position = 0;
}

int macro_queue_has_keys(void) {
    return E.macro_queue.position < E.macro_queue.length;
}

/* Map a base keycode to its canonical name used in <...> tokens. */
static const char *macro_named_key(int base) {
    switch (base) {
    case '\x1b':         return "Esc";
    case '\r':           return "CR";
    case '\n':           return "NL";
    case '\t':           return "Tab";
    case 127:            return "BS";
    case KEY_ARROW_UP:   return "Up";
    case KEY_ARROW_DOWN: return "Down";
    case KEY_ARROW_LEFT: return "Left";
    case KEY_ARROW_RIGHT:return "Right";
    case KEY_HOME:       return "Home";
    case KEY_END:        return "End";
    case KEY_PAGE_UP:    return "PageUp";
    case KEY_PAGE_DOWN:  return "PageDown";
    default:             return NULL;
    }
}

/* Reverse: token name -> base keycode. Returns -1 if unknown. */
static int macro_named_code(const char *name, int len) {
    static const struct { const char *name; int code; } names[] = {
        {"Esc", '\x1b'}, {"CR", '\r'}, {"NL", '\n'}, {"Tab", '\t'},
        {"BS", 127},
        {"Up", KEY_ARROW_UP}, {"Down", KEY_ARROW_DOWN},
        {"Left", KEY_ARROW_LEFT}, {"Right", KEY_ARROW_RIGHT},
        {"Home", KEY_HOME}, {"End", KEY_END},
        {"PageUp", KEY_PAGE_UP}, {"PageDown", KEY_PAGE_DOWN},
        {"lt", '<'}, {"gt", '>'},
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        int nlen = (int)strlen(names[i].name);
        if (nlen == len && strncmp(name, names[i].name, nlen) == 0)
            return names[i].code;
    }
    return -1;
}

/* Try to decode a <...> token at the current queue position. On success
 * fills *out_key and advances position past '>'. */
static int macro_try_decode_token(int *out_key) {
    const char *buf = E.macro_queue.buffer;
    int pos = E.macro_queue.position;
    int len = E.macro_queue.length;

    /* Bound the search so a stray '<' doesn't scan the whole buffer. */
    int limit = pos + 40;
    if (limit > len) limit = len;
    int end = pos + 1;
    while (end < limit && buf[end] != '>') end++;
    if (end >= limit || buf[end] != '>') return 0;

    int p = pos + 1;            /* inner start */
    int inner_end = end;        /* exclusive */
    if (inner_end <= p) return 0;

    /* Strip modifier prefixes in any order: M-, C-, S- */
    int flags = 0;
    while (p + 1 < inner_end && buf[p + 1] == '-') {
        char c = buf[p];
        if      (c == 'M') flags |= KEY_META;
        else if (c == 'C') flags |= KEY_CTRL;
        else if (c == 'S') flags |= KEY_SHIFT;
        else break;
        p += 2;
    }

    int name_len = inner_end - p;
    if (name_len <= 0) return 0;

    int base = -1;

    if (buf[p] == '#') {
        /* Numeric escape: <#NNNN> */
        if (name_len < 2) return 0;
        int n = 0;
        for (int i = p + 1; i < inner_end; i++) {
            if (buf[i] < '0' || buf[i] > '9') return 0;
            n = n * 10 + (buf[i] - '0');
            if (n > 0xFFFF) return 0;
        }
        base = n;
    } else if (buf[p] == 'F' && name_len >= 2 &&
               buf[p + 1] >= '0' && buf[p + 1] <= '9') {
        /* Function key: F1..F12 */
        int n = 0;
        for (int i = p + 1; i < inner_end; i++) {
            if (buf[i] < '0' || buf[i] > '9') return 0;
            n = n * 10 + (buf[i] - '0');
        }
        if (n < 1 || n > 12) return 0;
        base = KEY_F1 + n - 1;
    } else if (name_len == 1) {
        unsigned char c = (unsigned char)buf[p];
        if (c < 32 || c >= 127) return 0;
        base = c;
    } else {
        base = macro_named_code(buf + p, name_len);
        if (base < 0) return 0;
    }

    /* <C-a>..<C-z> with no other flags collapses to raw byte 1..26,
     * matching what the input parser produces for typed Ctrl-letter. */
    if (flags == KEY_CTRL && ((base >= 'a' && base <= 'z') ||
                              (base >= 'A' && base <= 'Z'))) {
        int letter = (base >= 'A' && base <= 'Z') ? base - 'A' + 1
                                                  : base - 'a' + 1;
        *out_key = letter;
    } else {
        *out_key = flags | base;
    }

    E.macro_queue.position = end + 1;
    return 1;
}

int macro_queue_get_key(void) {
    if (!macro_queue_has_keys())
        return 0;

    char c = E.macro_queue.buffer[E.macro_queue.position];
    if (c == '<') {
        int key;
        if (macro_try_decode_token(&key))
            return key;
        /* Malformed token: fall through and emit literal '<'. */
    }

    E.macro_queue.position++;
    return (unsigned char)c;
}

void macro_replay_string(const char *str, size_t len) {
    if (!str || len == 0)
        return;

    /* Clear any existing queue */
    macro_queue_clear();

    /* Ensure capacity */
    if (len > (size_t)E.macro_queue.capacity) {
        int new_cap = len + 256; /* Add some extra space */
        char *new_buf = realloc(E.macro_queue.buffer, new_cap);
        if (!new_buf)
            return; /* Out of memory */
        E.macro_queue.buffer = new_buf;
        E.macro_queue.capacity = new_cap;
    }

    /* Copy the string directly to the buffer */
    memcpy(E.macro_queue.buffer, str, len);
    E.macro_queue.length = len;
    E.macro_queue.position = 0;
}

/* Macro recording functions */

void macro_start_recording(char register_name) {
    if (register_name < 'a' || register_name > 'z')
        return;

    /* Clear the target register */
    regs_set_named(register_name, "", 0);

    /* Start recording */
    E.macro_recording.recording = 1;
    E.macro_recording.register_name = register_name;
}

void macro_stop_recording(void) {
    E.macro_recording.recording = 0;
    E.macro_recording.register_name = '\0';
}

int macro_is_recording(void) { return E.macro_recording.recording; }

char macro_get_recording_register(void) {
    return E.macro_recording.register_name;
}

/* Helper: Convert key code to string representation. Symmetric with the
 * <...> parser in macro_try_decode_token. */
static void key_to_string_buf(int key, char *buf, size_t bufsize) {
    int has_mod = KEY_IS_META(key) || KEY_IS_CTRL(key) || KEY_IS_SHIFT(key);

    if (!has_mod) {
        if (key >= 32 && key < 127 && key != '<') {
            snprintf(buf, bufsize, "%c", key);
            return;
        }
        if (key == '<') {
            snprintf(buf, bufsize, "<lt>");
            return;
        }
        const char *named = macro_named_key(key);
        if (named) {
            snprintf(buf, bufsize, "<%s>", named);
            return;
        }
        if (key >= 1 && key <= 26) {
            snprintf(buf, bufsize, "<C-%c>", key + 'a' - 1);
            return;
        }
        if (key >= KEY_F1 && key <= KEY_F12) {
            snprintf(buf, bufsize, "<F%d>", key - KEY_F1 + 1);
            return;
        }
        snprintf(buf, bufsize, "<#%d>", key);
        return;
    }

    int base = KEY_BASE(key);
    char prefix[8];
    int p = 0;
    prefix[p++] = '<';
    if (KEY_IS_META(key))  { prefix[p++] = 'M'; prefix[p++] = '-'; }
    if (KEY_IS_CTRL(key))  { prefix[p++] = 'C'; prefix[p++] = '-'; }
    if (KEY_IS_SHIFT(key)) { prefix[p++] = 'S'; prefix[p++] = '-'; }
    prefix[p] = '\0';

    const char *named = macro_named_key(base);
    if (named) {
        snprintf(buf, bufsize, "%s%s>", prefix, named);
    } else if (base >= KEY_F1 && base <= KEY_F12) {
        snprintf(buf, bufsize, "%sF%d>", prefix, base - KEY_F1 + 1);
    } else if (base >= 32 && base < 127) {
        snprintf(buf, bufsize, "%s%c>", prefix, base);
    } else {
        snprintf(buf, bufsize, "%s#%d>", prefix, base);
    }
}

void macro_record_key(int key) {
    if (!E.macro_recording.recording)
        return;

    /* Convert key to string representation */
    char key_str[32];
    key_to_string_buf(key, key_str, sizeof(key_str));

    /* Append to the recording register */
    regs_append_named(E.macro_recording.register_name, key_str,
                      strlen(key_str));
}

void macro_play(char register_name) {
    if (register_name < 'a' || register_name > 'z')
        return;

    /* Get the macro from the register */
    const SizedStr *reg = regs_get(register_name);
    if (!reg || !reg->data || reg->len == 0)
        return;

    /* Remember this as the last played macro */
    E.macro_recording.last_played = register_name;

    /* Replay through the macro queue */
    macro_replay_string(reg->data, reg->len);
}

void macro_play_last(void) {
    if (E.macro_recording.last_played == '\0')
        return;

    macro_play(E.macro_recording.last_played);
}
