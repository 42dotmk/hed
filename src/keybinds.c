#include "keybinds.h"
#include "editor.h"
#include "commands.h"
#include "hooks.h"
#include "lib/log.h"
#include "stb_ds.h"
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "registers.h"
#include "lib/safe_string.h"
#include "buf/textobj.h"
#include "keybinds_builtins.h"
#include <time.h>

#define KEY_BUFFER_SIZE 16
#define SEQUENCE_TIMEOUT_MS 5000

/* Keybinding entry */
typedef struct {
    char *sequence;
    KeybindCallback callback;
    CommandCallback command_callback; /* optional: invoked with cmdline */
    int mode;
    char *desc;     /* stores command line when using command_callback */
    char *filetype; /* NULL = global; non-NULL = filetype-specific */
} Keybind;

/* Global keybinding storage (stb_ds dynamic array) */
static Keybind *keybinds = NULL;

/* Input buffer for multi-key sequences */
static char key_buffer[KEY_BUFFER_SIZE];
static int key_buffer_len = 0;
static struct timespec last_key_time;
static int pending_count = 0; /* numeric prefix */
static int have_count = 0;
/* Helper: convert key code to string representation */
static void key_to_string(int key, char *buf, size_t bufsize) {
    if (KEY_IS_META(key) || KEY_IS_CTRL(key) || KEY_IS_SHIFT(key)) {
        int base = KEY_BASE(key);
        char prefix[16];
        int p = 0;
        prefix[p++] = '<';
        if (KEY_IS_META(key))  { prefix[p++] = 'M'; prefix[p++] = '-'; }
        if (KEY_IS_CTRL(key))  { prefix[p++] = 'C'; prefix[p++] = '-'; }
        if (KEY_IS_SHIFT(key)) { prefix[p++] = 'S'; prefix[p++] = '-'; }
        prefix[p] = '\0';

        const char *named = NULL;
        switch (base) {
        case KEY_ARROW_UP:    named = "Up";    break;
        case KEY_ARROW_DOWN:  named = "Down";  break;
        case KEY_ARROW_LEFT:  named = "Left";  break;
        case KEY_ARROW_RIGHT: named = "Right"; break;
        case KEY_HOME:        named = "Home";  break;
        case KEY_END:         named = "End";   break;
        }
        if (named) {
            snprintf(buf, bufsize, "%s%s>", prefix, named);
        } else if (base >= KEY_F1 && base <= KEY_F12) {
            snprintf(buf, bufsize, "%sF%d>", prefix, base - KEY_F1 + 1);
        } else if (base >= 32 && base < 127) {
            snprintf(buf, bufsize, "%s%c>", prefix, base);
        } else if (base >= 1 && base <= 26) {
            snprintf(buf, bufsize, "%s%c>", prefix, base + 'a' - 1);
        } else {
            snprintf(buf, bufsize, "%s%d>", prefix, base);
        }
        return;
    }
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
    } else if (key >= KEY_F1 && key <= KEY_F12) {
        snprintf(buf, bufsize, "<F%d>", key - KEY_F1 + 1);
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

/* Initialize keybinding system. User content is registered later from
 * config_init() (see src/config.c). Registration uses last-write-wins,
 * so the order in config_init determines precedence. */
void keybind_init(void) {
    arrfree(keybinds);
    keybinds = NULL;
    key_buffer_len = 0;
    key_buffer[0] = '\0';
    clock_gettime(CLOCK_MONOTONIC, &last_key_time);
}

void keybind_state_save(KeybindState *out) {
    if (!out) return;
    memcpy(out->key_buffer, key_buffer, sizeof(out->key_buffer));
    out->key_buffer_len = key_buffer_len;
    out->pending_count  = pending_count;
    out->have_count     = have_count;
}

void keybind_state_load(const KeybindState *in) {
    if (!in) return;
    memcpy(key_buffer, in->key_buffer, sizeof(key_buffer));
    key_buffer_len = in->key_buffer_len;
    pending_count  = in->pending_count;
    have_count     = in->have_count;
}

/* Drop any prior keybind with the same (mode, sequence, filetype) tuple so
 * that the latest registration wins. Lets users override plugin defaults. */
static void remove_duplicate(int mode, const char *sequence,
                             const char *filetype) {
    for (ptrdiff_t i = 0; i < arrlen(keybinds); i++) {
        if (keybinds[i].mode != mode) continue;
        if (strcmp(keybinds[i].sequence, sequence) != 0) continue;
        int same_ft =
            (keybinds[i].filetype == NULL && filetype == NULL) ||
            (keybinds[i].filetype && filetype &&
             strcmp(keybinds[i].filetype, filetype) == 0);
        if (!same_ft) continue;

        free(keybinds[i].sequence);
        free(keybinds[i].desc);
        free(keybinds[i].filetype);
        arrdel(keybinds, i);
        return; /* invariant: at most one match exists */
    }
}

/* Register a keybinding */
void keybind_register(int mode, const char *sequence,
                      KeybindCallback callback, const char *desc) {
    remove_duplicate(mode, sequence, NULL);
    Keybind kb = {
        .sequence         = strdup(sequence),
        .callback         = callback,
        .command_callback = NULL,
        .mode             = mode,
        .desc             = desc ? strdup(desc) : NULL,
        .filetype         = NULL,
    };
    arrput(keybinds, kb);
}

void keybind_register_ft(int mode, const char *sequence, const char *filetype,
                         KeybindCallback callback, const char *desc) {
    remove_duplicate(mode, sequence, filetype);
    Keybind kb = {
        .sequence         = strdup(sequence),
        .callback         = callback,
        .command_callback = NULL,
        .mode             = mode,
        .desc             = desc ? strdup(desc) : NULL,
        .filetype         = filetype ? strdup(filetype) : NULL,
    };
    arrput(keybinds, kb);
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
    remove_duplicate(mode, sequence, NULL);
    Keybind kb = {
        .sequence         = strdup(sequence),
        .callback         = NULL,
        .command_callback = kb_run_command,
        .mode             = mode,
        .desc             = cmdline ? strdup(cmdline) : strdup(""),
        .filetype         = NULL,
    };
    arrput(keybinds, kb);
}

void keybind_register_command_ft(int mode, const char *sequence,
                                  const char *filetype, const char *cmdline) {
    remove_duplicate(mode, sequence, filetype);
    Keybind kb = {
        .sequence         = strdup(sequence),
        .callback         = NULL,
        .command_callback = kb_run_command,
        .mode             = mode,
        .desc             = cmdline ? strdup(cmdline) : strdup(""),
        .filetype         = filetype ? strdup(filetype) : NULL,
    };
    arrput(keybinds, kb);
}

/* Clear the key buffer */
void keybind_clear_buffer(void) {
    key_buffer_len = 0;
    key_buffer[0] = '\0';
    pending_count = 0;
    have_count = 0;
}

/* Get the total number of registered keybindings */
int keybind_get_count(void) {
    return (int)arrlen(keybinds);
}

/* Get keybinding info at the given index */
int keybind_get_at(int index, const char **sequence, const char **desc, int *mode) {
    if (index < 0 || (ptrdiff_t)index >= arrlen(keybinds))
        return 0;

    if (sequence)
        *sequence = keybinds[index].sequence;
    if (desc)
        *desc = keybinds[index].desc;
    if (mode)
        *mode = keybinds[index].mode;

    return 1;
}

/* Get and consume the pending numeric count (for commands that read additional keys) */
int keybind_get_and_clear_pending_count(void) {
    int count = have_count ? pending_count : 1;
    if (count < 1)
        count = 1;
    pending_count = 0;
    have_count = 0;
    return count;
}

/* Build a view onto a registry entry for callers. */
static KeybindMatchView make_view(const Keybind *kb) {
    bool is_command = kb->command_callback != NULL;
    KeybindMatchView v = {
        .sequence          = kb->sequence,
        .desc              = kb->desc,
        .mode              = kb->mode,
        .is_command        = is_command,
        .filetype_specific = kb->filetype != NULL,
        .callback          = kb->callback,
        .command_callback  = kb->command_callback,
        .cmdline           = is_command ? kb->desc : NULL,
    };
    return v;
}

/* Feed one key into the dispatcher. Mutates the sequence buffer and
 * count state, but never runs a callback. */
KeybindFeedResult keybind_feed(int key, int mode) {
    KeybindFeedResult r = {0};

    /* Numeric prefix only applies in normal mode */
    if (mode != MODE_NORMAL) {
        pending_count = 0;
        have_count = 0;
    }

    /* Check timeout - if too much time passed, clear buffer */
    if (key_buffer_len > 0 && timeout_exceeded()) {
        keybind_clear_buffer();
    }

    clock_gettime(CLOCK_MONOTONIC, &last_key_time);

    /* Numeric prefix: consumed before any sequence char is appended. */
    if (mode == MODE_NORMAL && key_buffer_len == 0 &&
        key >= '0' && key <= '9' && (have_count || key != '0')) {
        int digit = key - '0';
        pending_count = pending_count * 10 + digit;
        if (pending_count > 1000000) {
            pending_count = 1000000;
            log_msg("Warning: numeric prefix capped at 1,000,000");
        }
        have_count = true;

        r.active_sequence     = key_buffer; /* still empty */
        r.active_len          = 0;
        r.count               = pending_count;
        r.has_count           = true;
        r.consumed_count_only = true;

        HookKeybindFeedEvent ev = {
            .active_sequence     = r.active_sequence,
            .active_len          = r.active_len,
            .count               = r.count,
            .has_count           = r.has_count,
            .consumed_count_only = true,
        };
        hook_fire_keybind_feed(HOOK_KEYBIND_FEED, &ev);
        return r;
    }

    /* Convert key to string and append to buffer */
    char key_str[128];
    key_to_string(key, key_str, sizeof(key_str));

    if (key_buffer_len + strlen(key_str) < KEY_BUFFER_SIZE - 1) {
        EdError err = safe_strcat(key_buffer, key_str, KEY_BUFFER_SIZE);
        if (err != ED_OK) {
            keybind_clear_buffer();
            safe_strcpy(key_buffer, key_str, KEY_BUFFER_SIZE);
        }
        key_buffer_len = strlen(key_buffer);
    } else {
        keybind_clear_buffer();
        safe_strcpy(key_buffer, key_str, KEY_BUFFER_SIZE);
        key_buffer_len = strlen(key_buffer);
    }

    /* Scan registry. Filetype-specific matches take priority over global
     * for the same sequence. Collect every (still-reachable) match —
     * exact or partial — for the caller to display. */
    int exact_global = -1;
    int exact_ft     = -1;

    const char *cur_ft = NULL;
    {
        Buffer *cb = buf_cur();
        if (cb) cur_ft = cb->filetype;
    }

    for (ptrdiff_t i = 0; i < arrlen(keybinds); i++) {
        if (keybinds[i].mode != mode) continue;

        bool ft_applicable =
            keybinds[i].filetype == NULL ||
            (cur_ft && strcmp(keybinds[i].filetype, cur_ft) == 0);
        if (!ft_applicable) continue;

        bool is_exact = strcmp(keybinds[i].sequence, key_buffer) == 0;
        bool is_prefix =
            !is_exact &&
            strncmp(keybinds[i].sequence, key_buffer, key_buffer_len) == 0;

        if (is_exact) {
            if (keybinds[i].filetype) {
                exact_ft = (int)i;
            } else if (exact_global < 0) {
                exact_global = (int)i; /* keep first global */
            }
        }

        if (is_exact || is_prefix) {
            arrput(r.matches, make_view(&keybinds[i]));
        }
    }

    int exact_idx = exact_ft >= 0 ? exact_ft : exact_global;

    r.active_sequence = key_buffer;
    r.active_len      = key_buffer_len;
    r.count           = pending_count;
    r.has_count       = have_count;
    r.exact           = exact_idx >= 0;
    if (r.exact) {
        r.exact_match = make_view(&keybinds[exact_idx]);
    }
    /* "Partial" = at least one *other* sequence could still be completed.
     * If only the exact match itself is in the list, there's no point
     * waiting for more keys. */
    r.partial = arrlen(r.matches) > (r.exact ? 1 : 0);

    /* Single unmapped key in NORMAL: caller may want operator_move. */
    if (!r.exact && !r.partial && mode == MODE_NORMAL && key_buffer_len == 1) {
        r.fallback_textobj = true;
    }

    HookKeybindFeedEvent ev = {
        .active_sequence     = r.active_sequence,
        .active_len          = r.active_len,
        .count               = r.count,
        .has_count           = r.has_count,
        .exact               = r.exact,
        .partial             = r.partial,
        .consumed_count_only = r.consumed_count_only,
        .matches             = r.matches,
        .match_count         = (int)arrlen(r.matches),
    };
    hook_fire_keybind_feed(HOOK_KEYBIND_FEED, &ev);

    return r;
}

void keybind_feed_result_free(KeybindFeedResult *r) {
    if (!r) return;
    arrfree(r->matches);
    r->matches = NULL;
}

void keybind_invoke(const KeybindMatchView *m, int repeat) {
    if (!m) return;
    if (repeat < 1) repeat = 1;

    HookKeybindInvokeEvent ev = { .match = m, .repeat = repeat };
    hook_fire_keybind_invoke(HOOK_KEYBIND_INVOKE, &ev);

    if (m->callback) {
        for (int i = 0; i < repeat; i++) m->callback();
    } else if (m->command_callback) {
        for (int i = 0; i < repeat; i++) m->command_callback(m->cmdline);
    } else {
        return;
    }

    /* Record the invoked sequence in the . register so dot-repeat
     * can replay it. */
    if (m->sequence) {
        regs_set_dot(m->sequence, strlen(m->sequence));
    }
}

/* Legacy single-call dispatch: feed + invoke on exact match, with
 * the NORMAL-mode single-key textobj fallback. */
bool keybind_process(int key, int mode) {
    KeybindFeedResult r = keybind_feed(key, mode);
    bool handled = false;

    if (r.exact) {
        int repeat = r.has_count ? r.count : 1;
        keybind_invoke(&r.exact_match, repeat);
        keybind_clear_buffer();
        handled = true;
    } else if (r.partial || r.consumed_count_only) {
        handled = true;
    } else if (r.fallback_textobj) {
        int fallback_key = key_buffer[0];
        keybind_clear_buffer();
        kb_operator_move(fallback_key);
        handled = true;
    } else {
        keybind_clear_buffer();
    }

    keybind_feed_result_free(&r);
    return handled;
}

/* ========================================================================
 * Text Object System
 * ======================================================================== */

#define MAX_TEXTOBJ_BINDS 128

/* Text object keybinding structure */
typedef struct {
    char keys[16];          /* Key sequence (e.g., "w", "iw", "aw") */
    TextObjFunc func;       /* Callback that fills TextSelection */
    char desc[128];         /* Description */
} TextObjKeybind;

/* Global text object storage */
static TextObjKeybind textobj_map[MAX_TEXTOBJ_BINDS];
static int textobj_count = 0;

/* Register a text object */
void textobj_register(const char *keys, TextObjFunc func, const char *desc) {
    if (textobj_count >= MAX_TEXTOBJ_BINDS) {
        log_msg("Warning: textobj registry full");
        return;
    }

    TextObjKeybind *kb = &textobj_map[textobj_count++];
    strncpy(kb->keys, keys, sizeof(kb->keys) - 1);
    kb->keys[sizeof(kb->keys) - 1] = '\0';
    kb->func = func;
    if (desc) {
        strncpy(kb->desc, desc, sizeof(kb->desc) - 1);
        kb->desc[sizeof(kb->desc) - 1] = '\0';
    } else {
        kb->desc[0] = '\0';
    }

    log_msg("Registered text object: %s - %s", keys, desc ? desc : "");
}

/* Lookup and invoke a text object by key sequence */
int textobj_lookup(const char *keys, Buffer *buf, int line, int col,
                   TextSelection *sel) {
    if (!keys || !buf || !sel) {
        return 0;
    }

    for (int i = 0; i < textobj_count; i++) {
        if (strcmp(textobj_map[i].keys, keys) == 0) {
            return textobj_map[i].func(buf, line, col, sel);
        }
    }
    return 0; /* Not found */
}
