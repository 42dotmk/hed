#include "keybinds.h"
#include "commands.h"
#include "hed.h"
#include "safe_string.h"
#include <time.h>

#define MAX_KEYBINDS 256
#define KEY_BUFFER_SIZE 16
#define SEQUENCE_TIMEOUT_MS 1000  /* 1 second timeout for multi-key sequences */

/* Keybinding entry */
typedef struct {
    char *sequence;
    KeybindCallback callback;
    CommandCallback command_callback; /* optional: invoked with cmdline */
    int mode;
    char *desc; /* stores command line when using command_callback */
} Keybind;

/* Global keybinding storage */
static Keybind keybinds[MAX_KEYBINDS];
static int keybind_count = 0;

/* Input buffer for multi-key sequences */
static char key_buffer[KEY_BUFFER_SIZE];
static int key_buffer_len = 0;
static struct timespec last_key_time;

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
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    long elapsed_ms = (now.tv_sec - last_key_time.tv_sec) * 1000 +
                      (now.tv_nsec - last_key_time.tv_nsec) / 1000000;

    return elapsed_ms > SEQUENCE_TIMEOUT_MS;
}

/*** Default keybinding callbacks ***/

/* Normal mode - mode switching */

void kb_enter_insert_mode(void) {
    ed_set_mode(MODE_INSERT);
}

void kb_append_mode(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!buf || !win) return;

    ed_set_mode(MODE_INSERT);
    if (win->cursor_y < buf->num_rows) {
        Row *row = &buf->rows[win->cursor_y];
        if (win->cursor_x < (int)row->chars.len) win->cursor_x++;
    }
}

void kb_enter_visual_mode(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!buf || !win) return;

    ed_set_mode(MODE_VISUAL);
    win->visual_start_x = win->cursor_x;
    win->visual_start_y = win->cursor_y;
    ed_set_status_message("-- VISUAL --");
}

void kb_enter_command_mode(void) {
    extern Ed E;
    ed_set_mode(MODE_COMMAND);
    E.command_len = 0;
}

/* Normal mode - text operations */
void kb_delete_line(void) {
    Buffer *buf = buf_cur(); if (!buf) return;
    buf_delete_line_in(buf);
}

void kb_yank_line(void) {
    Buffer *buf = buf_cur(); if (!buf) return;
    buf_yank_line_in(buf);
    ed_set_status_message("Yanked");
}

void kb_paste(void) {
    Buffer *buf = buf_cur(); if (!buf) return;
    buf_paste_in(buf);
}

void kb_delete_char(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!buf || !win) return;

    if (win->cursor_y < buf->num_rows) {
        Row *row = &buf->rows[win->cursor_y];
        if (win->cursor_x < (int)row->chars.len) {
            buf_row_del_char_in(buf, row, win->cursor_x);
        }
    }
}

/* Normal mode - cursor movement */
void kb_cursor_line_start(void) {
    Window *win = window_cur();
    if (!win) return;
    win->cursor_x = 0;
}

void kb_cursor_line_end(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!buf || !win) return;
    if (win->cursor_y < buf->num_rows) {
        win->cursor_x = buf->rows[win->cursor_y].chars.len;
    }
}

void kb_cursor_top(void) {
    Window *win = window_cur();
    if (!win) return;
    win->cursor_y = 0;
    win->cursor_x = 0;
}

void kb_cursor_bottom(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!buf || !win) return;
    win->cursor_y = buf->num_rows - 1;
    if (win->cursor_y < 0) win->cursor_y = 0;
    win->cursor_x = 0;
}

/* Normal mode - search */
void kb_search_next(void) {
    Buffer *buf = buf_cur(); if (!buf) return;
    buf_find_in(buf);
}

void kb_find_under_cursor(void) {
    SizedStr w = sstr_new();
    if (!buf_get_word_under_cursor(&w)) { sstr_free(&w); return; }
    sstr_free(&E.search_query);
    E.search_query = sstr_from(w.data, w.len);
    ed_set_status_message("* %.*s", (int)(w.len > 40 ? 40 : w.len), w.data);
    sstr_free(&w);
    buf_find_in(buf_cur());
}

void kb_line_number_toggle(void) {
    cmd_ln(NULL);
}
/* Visual mode operations */
void kb_visual_yank(void) {
    Buffer *buf = buf_cur(); if (!buf) return;
    buf_yank_line_in(buf);
    ed_set_mode(MODE_NORMAL);
    ed_set_status_message("Yanked");
}

void kb_visual_delete(void) {
    Buffer *buf = buf_cur(); if (!buf) return;
    buf_delete_line_in(buf);
    ed_set_mode(MODE_NORMAL);
    ed_set_status_message("Deleted");
}

/* Undo/Redo */
void kb_undo(void) {
    if (undo_perform()) {
        ed_set_status_message("Undid");
    } else {
        ed_set_status_message("Nothing to undo");
    }
}

void kb_redo(void) {
    if (redo_perform()) {
        ed_set_status_message("Redid");
    } else {
        ed_set_status_message("Nothing to redo");
    }
}

/* Helper: perform jump in specified direction */
static void kb_jump(int direction) {
    int buffer_idx, cursor_x, cursor_y;
    int success;
    const char *direction_str;
    const char *limit_msg;

    if (direction < 0) {
        success = jump_list_backward(&E.jump_list, &buffer_idx, &cursor_x, &cursor_y);
        direction_str = "back";
        limit_msg = "Already at oldest jump position";
    } else {
        success = jump_list_forward(&E.jump_list, &buffer_idx, &cursor_x, &cursor_y);
        direction_str = "forward";
        limit_msg = "Already at newest jump position";
    }

    if (success) {
        /* Switch to the buffer without adding to jump list */
        if (buffer_idx >= 0 && buffer_idx < (int)E.buffers.len) {
            E.current_buffer = buffer_idx;
            Window *win = window_cur();
            if (win) {
                win->buffer_index = buffer_idx;
                win->cursor_x = cursor_x;
                win->cursor_y = cursor_y;
            }

            Buffer *buf = buf_cur();
            ed_set_status_message("Jumped %s to buffer %d: %s", direction_str,
                buffer_idx + 1, buf->title);
        } else {
            ed_set_status_message("Jump target buffer no longer exists");
        }
    } else {
        ed_set_status_message("%s", limit_msg);
    }
}

void kb_jump_backward(void) {
    kb_jump(-1);
}

void kb_jump_forward(void) {
    kb_jump(1);
}


/* Initialize keybinding system */
void keybind_init(void) {
    keybind_count = 0;
    key_buffer_len = 0;
    key_buffer[0] = '\0';
    clock_gettime(CLOCK_MONOTONIC, &last_key_time);
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
    keybinds[keybind_count].command_callback = NULL;
    keybinds[keybind_count].desc = NULL;
    keybind_count++;
}

static void kb_run_command(const char *cmdline) {
    if (!cmdline) return;
    while (*cmdline == ' ' || *cmdline == '\t' || *cmdline == ':') cmdline++;
    const char *p = cmdline;
    while (*p && *p != ' ' && *p != '\t') p++;
    if (p == cmdline) return;
    char name[64];
    size_t n = (size_t)(p - cmdline); if (n >= sizeof(name)) n = sizeof(name) - 1;
    memcpy(name, cmdline, n); name[n] = '\0';
    while (*p == ' ' || *p == '\t') p++;
    const char *args = (*p ? p : NULL);
    command_invoke(name, args);
}

void keybind_register_command(int mode, const char *sequence, const char *cmdline) {
    if (keybind_count >= MAX_KEYBINDS) return;
    keybinds[keybind_count].sequence = strdup(sequence);
    keybinds[keybind_count].callback = NULL;
    keybinds[keybind_count].command_callback = kb_run_command;
    keybinds[keybind_count].mode = mode;
    keybinds[keybind_count].desc = cmdline ? strdup(cmdline) : strdup("");
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
    clock_gettime(CLOCK_MONOTONIC, &last_key_time);

    /* Convert key to string and append to buffer */
    char key_str[32];
    key_to_string(key, key_str, sizeof(key_str));

    /* Append to buffer if there's space */
    if (key_buffer_len + strlen(key_str) < KEY_BUFFER_SIZE - 1) {
        EdError err = safe_strcat(key_buffer, key_str, KEY_BUFFER_SIZE);
        if (err != ED_OK) {
            /* Shouldn't happen due to check above, but handle gracefully */
            keybind_clear_buffer();
            safe_strcpy(key_buffer, key_str, KEY_BUFFER_SIZE);
        }
        key_buffer_len = strlen(key_buffer);
    } else {
        /* Buffer full, clear and start over */
        keybind_clear_buffer();
        safe_strcpy(key_buffer, key_str, KEY_BUFFER_SIZE);
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

    /* Exact match found - execute action */
    if (exact_match >= 0) {
        if (keybinds[exact_match].callback) {
            keybinds[exact_match].callback();
        } else if (keybinds[exact_match].command_callback) {
            keybinds[exact_match].command_callback(keybinds[exact_match].desc);
        }
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
