#ifndef KEYBINDS_H
#define KEYBINDS_H
#include "editor.h"
#include "commands.h"
#include <stdbool.h>

/* Keybinding callback signature */
typedef void (*KeybindCallback)(void);

/* Keybinding API */

/**
 * Initialize the keybinding system
 * Called once during editor initialization
 */
void keybind_init(void);

/**
 * Register a keybinding
 *
 * @param mode     Editor mode (MODE_NORMAL, MODE_INSERT, etc.)
 * @param sequence Key sequence or combo:
 *                 - Single key: "x", "p", "i"
 *                 - Multi-key: "dd", "gg", "yy"
 *                 - Ctrl combo: "<C-s>", "<C-w>", "<C-q>"
 * @param callback Function to call when keybind is triggered
 * @param desc     Short description of what this keybind does
 *
 * Examples:
 *   keybind_register(MODE_NORMAL, "x", delete_char, "del char");
 *   keybind_register(MODE_NORMAL, "dd", delete_line, "del line");
 *   keybind_register(MODE_NORMAL, "<C-s>", save_file, "save");
 */
void keybind_register(int mode, const char *sequence, KeybindCallback callback,
                      const char *desc);

/* Filetype-specific variant — only fires when the current buffer's filetype
 * matches. Takes priority over a global binding on the same sequence. */
void keybind_register_ft(int mode, const char *sequence, const char *filetype,
                         KeybindCallback callback, const char *desc);

/**
 * Register a keybinding that invokes a command (like typing :<name> <args>)
 *
 * @param mode     Editor mode (MODE_NORMAL, MODE_INSERT, etc.)
 * @param sequence Key sequence (e.g., "ff", "<C-r>")
 * @param cmdline  Command string to invoke (e.g., "echo Hello" or "rg TODO")
 */
void keybind_register_command(int mode, const char *sequence,
                              const char *cmdline);

/* Filetype-specific command keybinding variant. */
void keybind_register_command_ft(int mode, const char *sequence,
                                  const char *filetype, const char *cmdline);

/**
 * Process a key press through the keybinding system
 *
 * @param key  The key code from ed_read_key()
 * @param mode Current editor mode
 * @return 1 if keybind was matched and executed, 0 if not matched
 */
bool keybind_process(int key, int mode);

/* ------------------------------------------------------------------ */
/* Two-phase API: feed a key, then optionally invoke the exact match. */
/* keybind_process() is a thin wrapper around feed + invoke that      */
/* preserves the legacy single-call dispatch.                         */
/* ------------------------------------------------------------------ */

/* A single keybinding visible to a feed result. Pointers reference
 * registry-owned storage and stay valid until the binding is replaced
 * via re-registration; do not free them. The callback/command_callback
 * fields are the invoke handle — pass a view to keybind_invoke(). */
typedef struct KeybindMatchView {
    const char     *sequence;
    const char     *desc;
    int             mode;
    bool            is_command;        /* runs a :command instead of a callback */
    bool            filetype_specific; /* registered with a non-NULL filetype */
    /* Invoke handle. Treat as opaque from the UI side. */
    KeybindCallback callback;
    CommandCallback command_callback;
    const char     *cmdline;           /* for command bindings (== desc); NULL otherwise */
} KeybindMatchView;

typedef struct KeybindFeedResult {
    /* Sequence accumulated so far (NUL-terminated, possibly empty). */
    const char *active_sequence;
    int         active_len;

    /* Numeric prefix typed so far (Vim-style count). Meaningful only
     * when has_count is true. */
    int  count;
    bool has_count;

    /* True when active_sequence is itself a registered keybinding —
     * pass exact_match to keybind_invoke() to execute it. */
    bool exact;
    KeybindMatchView exact_match; /* valid iff exact */

    /* True when at least one *other* registered keybinding has
     * active_sequence as a strict prefix (more keys could complete
     * a different binding). */
    bool partial;

    /* The key was swallowed as part of the numeric prefix and the
     * sequence buffer is still empty. matches is empty in this case
     * to avoid dumping the entire keymap. */
    bool consumed_count_only;

    /* Single unmapped key in NORMAL mode: keybind_process() falls
     * back to operator_move. Exposed here so callers driving the
     * API directly can decide for themselves. */
    bool fallback_textobj;

    /* stb_ds dynamic array of all matches for the current
     * active_sequence (empty when active_sequence is empty). Includes
     * both the exact match and sequences still waiting to complete.
     * Free with keybind_feed_result_free. */
    KeybindMatchView *matches;
} KeybindFeedResult;

/* Feed one key into the dispatcher. Updates the internal sequence
 * buffer and numeric prefix and returns a snapshot of the resulting
 * state. Does NOT run any callback. */
KeybindFeedResult keybind_feed(int key, int mode);

/* Free the matches array attached to a feed result. */
void keybind_feed_result_free(KeybindFeedResult *r);

/* Execute a specific keybinding `repeat` times. Stateless: does not
 * read or modify the sequence buffer or the numeric prefix; the
 * caller decides which binding to run and how many times. Updates
 * the `.` register so a subsequent dot-repeat can replay the
 * sequence. `repeat` is clamped to >= 1.
 *
 * Typical use: pass &result.exact_match from a feed result, with
 * `repeat = result.has_count ? result.count : 1`. */
void keybind_invoke(const KeybindMatchView *match, int repeat);

/**
 * Clear the current key sequence buffer
 * Called when mode changes or timeout occurs
 */
void keybind_clear_buffer(void);

/* Snapshot/restore of dispatch state (key sequence buffer, pending count).
 * Used by the multicursor plugin to replay a single keypress at every
 * cursor with the same starting state. */
typedef struct KeybindState {
    char key_buffer[16]; /* matches KEY_BUFFER_SIZE in keybinds.c */
    int  key_buffer_len;
    int  pending_count;
    int  have_count;
} KeybindState;

void keybind_state_save(KeybindState *out);
void keybind_state_load(const KeybindState *in);

/**
 * Get the total number of registered keybindings
 */
int keybind_get_count(void);

/**
 * Get keybinding info at the given index
 * Returns 1 if successful, 0 if index out of bounds
 */
int keybind_get_at(int index, const char **sequence, const char **desc, int *mode);

/**
 * Get and consume the pending numeric count
 * Used by commands that need to read additional keys after the command key
 * (e.g., @ for macro playback reads a register name after)
 * Returns the count (defaults to 1 if no count was entered)
 */
int keybind_get_and_clear_pending_count(void);

/* Text Object System */

/* Forward declarations */
struct Buffer;
struct TextSelection;

/**
 * Text object callback signature
 * @param buf  Buffer to operate on
 * @param line Cursor line position
 * @param col  Cursor column position
 * @param sel  Output TextSelection to fill
 * @return 1 if successful, 0 if text object not found or invalid
 */
typedef int (*TextObjFunc)(struct Buffer *buf, int line, int col,
                           struct TextSelection *sel);

/**
 * Register a text object keybinding
 * @param keys Key sequence (e.g., "w", "iw", "aw", "$")
 * @param func Callback that creates TextSelection
 * @param desc Description of the text object
 */
void textobj_register(const char *keys, TextObjFunc func, const char *desc);

/**
 * Lookup and invoke a text object by key sequence
 * @param keys Key sequence to look up
 * @param buf  Buffer to operate on
 * @param line Cursor line position
 * @param col  Cursor column position
 * @param sel  Output TextSelection to fill
 * @return 1 if text object found and executed successfully, 0 otherwise
 */
int textobj_lookup(const char *keys, struct Buffer *buf, int line, int col,
                   struct TextSelection *sel);

/* Built-in keybinding callbacks live in keybinds_builtins.h */

/* Convenience macros — used by plugins and config.c */
#define mapn(x, y, d)    keybind_register(MODE_NORMAL, x, y, d)
#define mapv(x, y, d)    keybind_register(MODE_VISUAL, x, y, d)
#define mapi(x, y, d)    keybind_register(MODE_INSERT, x, y, d)
#define mapvb(x, y, d)   keybind_register(MODE_VISUAL_BLOCK, x, y, d)
#define mapvl(x, y, d)   keybind_register(MODE_VISUAL_LINE, x, y, d)
#define cmapn(x, y)      keybind_register_command(MODE_NORMAL, x, y)
#define cmapv(x, y)      keybind_register_command(MODE_VISUAL, x, y)
#define cmapi(x, y)      keybind_register_command(MODE_INSERT, x, y)
#define mapn_ft(ft, x, y, d)  keybind_register_ft(MODE_NORMAL, x, ft, y, d)
#define mapi_ft(ft, x, y, d)  keybind_register_ft(MODE_INSERT, x, ft, y, d)
#define mapv_ft(ft, x, y, d)  keybind_register_ft(MODE_VISUAL, x, ft, y, d)
#define cmapn_ft(ft, x, y)    keybind_register_command_ft(MODE_NORMAL, x, ft, y)
#define cmapi_ft(ft, x, y)    keybind_register_command_ft(MODE_INSERT, x, ft, y)
#define cmapv_ft(ft, x, y)    keybind_register_command_ft(MODE_VISUAL, x, ft, y)

#endif // KEYBINDS_H
