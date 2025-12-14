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

int macro_queue_get_key(void) {
    if (!macro_queue_has_keys())
        return 0;

    char c = E.macro_queue.buffer[E.macro_queue.position];

    /* Check for special key sequences like <Esc>, <Up>, etc. */
    if (c == '<') {
        const char *rest = E.macro_queue.buffer + E.macro_queue.position + 1;
        int remaining = E.macro_queue.length - E.macro_queue.position - 1;

        /* Try to match special sequences */
        if (remaining >= 4 && strncmp(rest, "Esc>", 4) == 0) {
            E.macro_queue.position += 5;
            return '\x1b';
        } else if (remaining >= 3 && strncmp(rest, "CR>", 3) == 0) {
            E.macro_queue.position += 4;
            return '\r';
        } else if (remaining >= 4 && strncmp(rest, "Tab>", 4) == 0) {
            E.macro_queue.position += 5;
            return '\t';
        } else if (remaining >= 3 && strncmp(rest, "BS>", 3) == 0) {
            E.macro_queue.position += 4;
            return 127;
        } else if (remaining >= 5 && strncmp(rest, "Left>", 5) == 0) {
            E.macro_queue.position += 6;
            return KEY_ARROW_LEFT;
        } else if (remaining >= 6 && strncmp(rest, "Right>", 6) == 0) {
            E.macro_queue.position += 7;
            return KEY_ARROW_RIGHT;
        } else if (remaining >= 3 && strncmp(rest, "Up>", 3) == 0) {
            E.macro_queue.position += 4;
            return KEY_ARROW_UP;
        } else if (remaining >= 5 && strncmp(rest, "Down>", 5) == 0) {
            E.macro_queue.position += 6;
            return KEY_ARROW_DOWN;
        } else if (remaining >= 2 && rest[0] == 'C' && rest[1] == '-') {
            /* Ctrl sequences like <C-s>, <C-w> */
            if (remaining >= 4 && rest[3] == '>') {
                char ctrl_char = rest[2];
                if (ctrl_char >= 'a' && ctrl_char <= 'z') {
                    E.macro_queue.position += 5;
                    return ctrl_char - 'a' + 1;
                }
            }
        }
    }

    /* Regular character */
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

/* Helper: Convert key code to string representation */
static void key_to_string_buf(int key, char *buf, size_t bufsize) {
    if (key >= 32 && key < 127 && key != '<') {
        /* Printable ASCII (except '<' which we escape) */
        snprintf(buf, bufsize, "%c", key);
    } else if (key == 127) {
        snprintf(buf, bufsize, "<BS>");
    } else if (key == '\r') {
        snprintf(buf, bufsize, "<CR>");
    } else if (key == '\n') {
        snprintf(buf, bufsize, "<CR>");
    } else if (key == '\t') {
        snprintf(buf, bufsize, "<Tab>");
    } else if (key == '\x1b') {
        snprintf(buf, bufsize, "<Esc>");
    } else if (key == KEY_ARROW_UP) {
        snprintf(buf, bufsize, "<Up>");
    } else if (key == KEY_ARROW_DOWN) {
        snprintf(buf, bufsize, "<Down>");
    } else if (key == KEY_ARROW_LEFT) {
        snprintf(buf, bufsize, "<Left>");
    } else if (key == KEY_ARROW_RIGHT) {
        snprintf(buf, bufsize, "<Right>");
    } else if (key >= 1 && key <= 26) {
        /* Ctrl+letter */
        snprintf(buf, bufsize, "<C-%c>", key + 'a' - 1);
    } else if (key == '<') {
        /* Escape literal '<' */
        snprintf(buf, bufsize, "<%c>", key);
    } else {
        /* Unknown key - store as numeric code */
        snprintf(buf, bufsize, "<%d>", key);
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
