#include "keybinds.h"
#include "hed.h"
#include "safe_string.h"
#include <time.h>

#define MAX_KEYBINDS 256
#define KEY_BUFFER_SIZE 16
#define SEQUENCE_TIMEOUT_MS 1000 /* 1 second timeout for multi-key sequences   \
                                  */

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
static int pending_count = 0; /* numeric prefix */
static int have_count = 0;
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
    } else if (key == KEY_ARROW_UP) {
        snprintf(buf, bufsize, "<Up>");
    } else if (key == KEY_ARROW_DOWN) {
        snprintf(buf, bufsize, "<Down>");
    } else if (key == KEY_ARROW_LEFT) {
        snprintf(buf, bufsize, "<Left>");
    } else if (key == KEY_ARROW_RIGHT) {
        snprintf(buf, bufsize, "<Right>");
    } else if (key == KEY_HOME) {
        snprintf(buf, bufsize, "<Home>");
    } else if (key == KEY_END) {
        snprintf(buf, bufsize, "<End>");
    } else if (key == KEY_PAGE_UP) {
        snprintf(buf, bufsize, "<PageUp>");
    } else if (key == KEY_PAGE_DOWN) {
        snprintf(buf, bufsize, "<PageDown>");
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

/* Initialize keybinding system */
void keybind_init(void) {
    keybind_count = 0;
    key_buffer_len = 0;
    key_buffer[0] = '\0';
    clock_gettime(CLOCK_MONOTONIC, &last_key_time);
    user_keybinds_init();
}

/* Register a keybinding */
void keybind_register(int mode, const char *sequence,
                      KeybindCallback callback) {
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
    if (!cmdline)
        return;
    while (*cmdline == ' ' || *cmdline == '\t' || *cmdline == ':')
        cmdline++;
    const char *p = cmdline;
    while (*p && *p != ' ' && *p != '\t')
        p++;
    if (p == cmdline)
        return;
    char name[64];
    size_t n = (size_t)(p - cmdline);
    if (n >= sizeof(name))
        n = sizeof(name) - 1;
    memcpy(name, cmdline, n);
    name[n] = '\0';
    while (*p == ' ' || *p == '\t')
        p++;
    const char *args = (*p ? p : NULL);
    command_invoke(name, args);
}

void keybind_register_command(int mode, const char *sequence,
                              const char *cmdline) {
    if (keybind_count >= MAX_KEYBINDS)
        return;
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
    pending_count = 0;
    have_count = 0;
}

/* Process a key press through the keybinding system */
int keybind_process(int key, int mode) {
    /* Numeric prefix only applies in normal mode */
    if (mode != MODE_NORMAL) {
        pending_count = 0;
        have_count = 0;
    }

    /* Check timeout - if too much time passed, clear buffer */
    if (key_buffer_len > 0 && timeout_exceeded()) {
        keybind_clear_buffer();
    }

    /* Update timestamp */
    clock_gettime(CLOCK_MONOTONIC, &last_key_time);

    /* Numeric prefix handling (normal mode, idle buffer) */
    if (mode == MODE_NORMAL && key_buffer_len == 0) {
        if (key >= '0' && key <= '9') {
            if (have_count || key != '0') {
                int digit = key - '0';
                pending_count = pending_count * 10 + digit;
                if (pending_count > 1000000) {
                    pending_count = 1000000;
                    log_msg("Warning: numeric prefix capped at 1,000,000");
                }
                have_count = TRUE;
                return TRUE; /* consume digit, wait for next key */
            }
        }
    }

    /* Convert key to string and append to buffer */
    char key_str[128];
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
        int repeat = have_count ? pending_count : 1;
        if (repeat < 1)
            repeat = 1;
        if (keybinds[exact_match].callback) {
            for (int r = 0; r < repeat; r++) {
                keybinds[exact_match].callback();
            }
        } else if (keybinds[exact_match].command_callback) {
            for (int r = 0; r < repeat; r++) {
                keybinds[exact_match].command_callback(
                    keybinds[exact_match].desc);
            }
        }
        keybind_clear_buffer();
        return 1;
    }

    /* Partial match - wait for more keys */
    if (partial_match) {
        return 1; /* Consumed the key, waiting for more */
    }

    /* No match - clear buffer and return 0 (not handled) */
    keybind_clear_buffer();
    return 0;
}
