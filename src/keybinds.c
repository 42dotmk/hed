#include "keybinds.h"
#include "hed.h"
#include <sys/time.h>

#define MAX_KEYBINDS 256
#define KEY_BUFFER_SIZE 16
#define SEQUENCE_TIMEOUT_MS 1000  /* 1 second timeout for multi-key sequences */

/* Keybinding entry */
typedef struct {
    char *sequence;
    KeybindCallback callback;
    int mode;
} Keybind;

/* Global keybinding storage */
static Keybind keybinds[MAX_KEYBINDS];
static int keybind_count = 0;

/* Input buffer for multi-key sequences */
static char key_buffer[KEY_BUFFER_SIZE];
static int key_buffer_len = 0;
static struct timeval last_key_time;

/* Helper: convert key code to string representation */
static void key_to_string(int key, char *buf, size_t bufsize) {
    if (key >= 32 && key < 127) {
        /* Printable ASCII */
        snprintf(buf, bufsize, "%c", key);
    } else if (key == 127) {
        snprintf(buf, bufsize, "<BS>");
    } else if (key == '\r') {
        snprintf(buf, bufsize, "<CR>");
    } else if (key == '\n') {
        snprintf(buf, bufsize, "<NL>");
    } else if (key == '\t') {
        snprintf(buf, bufsize, "<Tab>");
    } else if (key == '\x1b') {
        snprintf(buf, bufsize, "<Esc>");
    } else if (key >= 1 && key <= 26) {
        /* Ctrl+letter */
        snprintf(buf, bufsize, "<C-%c>", key + 'a' - 1);
    } else {
        /* Unknown key */
        snprintf(buf, bufsize, "<%d>", key);
    }
}

/* Helper: check if elapsed time exceeds timeout */
static int timeout_exceeded(void) {
    struct timeval now;
    gettimeofday(&now, NULL);

    long elapsed_ms = (now.tv_sec - last_key_time.tv_sec) * 1000 +
                      (now.tv_usec - last_key_time.tv_usec) / 1000;

    return elapsed_ms > SEQUENCE_TIMEOUT_MS;
}

/*** Default keybinding callbacks ***/

/* Normal mode - mode switching */
void kb_enter_insert_mode(void) {
    ed_set_mode(MODE_INSERT);
}

void kb_append_mode(void) {
    Buffer *buf = buf_cur();
    if (!buf) return;

    ed_set_mode(MODE_INSERT);
    if (buf->cursor_y < buf->num_rows) {
        Row *row = &buf->rows[buf->cursor_y];
        if (buf->cursor_x < (int)row->chars.len) buf->cursor_x++;
    }
}

void kb_enter_visual_mode(void) {
    Buffer *buf = buf_cur();
    if (!buf) return;

    ed_set_mode(MODE_VISUAL);
    buf->visual_start_x = buf->cursor_x;
    buf->visual_start_y = buf->cursor_y;
}

void kb_enter_command_mode(void) {
    extern Ed E;
    ed_set_mode(MODE_COMMAND);
    E.command_len = 0;
}

/* Normal mode - text operations */
void kb_delete_line(void) {
    buf_delete_line();
}

void kb_yank_line(void) {
    buf_yank_line();
    ed_set_status_message("Yanked");
}

void kb_paste(void) {
    buf_paste();
}

void kb_delete_char(void) {
    Buffer *buf = buf_cur();
    if (!buf) return;

    if (buf->cursor_y < buf->num_rows) {
        Row *row = &buf->rows[buf->cursor_y];
        if (buf->cursor_x < (int)row->chars.len) {
            buf_row_del_char(row, buf->cursor_x);
        }
    }
}

/* Normal mode - cursor movement */
void kb_cursor_line_start(void) {
    Buffer *buf = buf_cur();
    if (!buf) return;
    buf->cursor_x = 0;
}

void kb_cursor_line_end(void) {
    Buffer *buf = buf_cur();
    if (!buf) return;
    if (buf->cursor_y < buf->num_rows) {
        buf->cursor_x = buf->rows[buf->cursor_y].chars.len;
    }
}

void kb_cursor_top(void) {
    Buffer *buf = buf_cur();
    if (!buf) return;
    buf->cursor_y = 0;
    buf->cursor_x = 0;
}

void kb_cursor_bottom(void) {
    Buffer *buf = buf_cur();
    if (!buf) return;
    buf->cursor_y = buf->num_rows - 1;
    if (buf->cursor_y < 0) buf->cursor_y = 0;
    buf->cursor_x = 0;
}

/* Normal mode - search */
void kb_search_next(void) {
    buf_find();
}

/* Visual mode operations */
void kb_visual_yank(void) {
    buf_yank_line();
    ed_set_mode(MODE_NORMAL);
    ed_set_status_message("Yanked");
}

void kb_visual_delete(void) {
    buf_delete_line();
    ed_set_mode(MODE_NORMAL);
    ed_set_status_message("Deleted");
}

/* Initialize keybinding system */
void keybind_init(void) {
    keybind_count = 0;
    key_buffer_len = 0;
    key_buffer[0] = '\0';
    gettimeofday(&last_key_time, NULL);
    user_keybinds_init();
}

/* Register a keybinding */
void keybind_register(int mode, const char *sequence, KeybindCallback callback) {
    if (keybind_count >= MAX_KEYBINDS) {
        return;
    }

    keybinds[keybind_count].sequence = strdup(sequence);
    keybinds[keybind_count].callback = callback;
    keybinds[keybind_count].mode = mode;
    keybind_count++;
}

/* Clear the key buffer */
void keybind_clear_buffer(void) {
    key_buffer_len = 0;
    key_buffer[0] = '\0';
}

/* Process a key press through the keybinding system */
int keybind_process(int key, int mode) {
    /* Check timeout - if too much time passed, clear buffer */
    if (key_buffer_len > 0 && timeout_exceeded()) {
        keybind_clear_buffer();
    }

    /* Update timestamp */
    gettimeofday(&last_key_time, NULL);

    /* Convert key to string and append to buffer */
    char key_str[32];
    key_to_string(key, key_str, sizeof(key_str));

    /* Append to buffer if there's space */
    if (key_buffer_len + strlen(key_str) < KEY_BUFFER_SIZE - 1) {
        strcat(key_buffer, key_str);
        key_buffer_len = strlen(key_buffer);
    } else {
        /* Buffer full, clear and start over */
        keybind_clear_buffer();
        strcpy(key_buffer, key_str);
        key_buffer_len = strlen(key_buffer);
    }

    /* Check for exact matches */
    int exact_match = -1;
    int partial_match = 0;

    for (int i = 0; i < keybind_count; i++) {
        /* Skip if mode doesn't match */
        if (keybinds[i].mode != mode) {
            continue;
        }

        /* Check for exact match */
        if (strcmp(keybinds[i].sequence, key_buffer) == 0) {
            exact_match = i;
            break;
        }

        /* Check for partial match (sequence starts with buffer) */
        if (strncmp(keybinds[i].sequence, key_buffer, key_buffer_len) == 0) {
            partial_match = 1;
        }
    }

    /* Exact match found - execute callback */
    if (exact_match >= 0) {
        keybinds[exact_match].callback();
        keybind_clear_buffer();
        return 1;
    }

    /* Partial match - wait for more keys */
    if (partial_match) {
        return 1;  /* Consumed the key, waiting for more */
    }

    /* No match - clear buffer and return 0 (not handled) */
    keybind_clear_buffer();
    return 0;
}
