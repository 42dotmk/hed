#ifndef KEYBINDS_H
#define KEYBINDS_H

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

/**
 * Register a keybinding that invokes a command (like typing :<name> <args>)
 *
 * @param mode     Editor mode (MODE_NORMAL, MODE_INSERT, etc.)
 * @param sequence Key sequence (e.g., "ff", "<C-r>")
 * @param cmdline  Command string to invoke (e.g., "echo Hello" or "rg TODO")
 */
void keybind_register_command(int mode, const char *sequence,
                              const char *cmdline);

/**
 * Process a key press through the keybinding system
 *
 * @param key  The key code from ed_read_key()
 * @param mode Current editor mode
 * @return 1 if keybind was matched and executed, 0 if not matched
 */
bool keybind_process(int key, int mode);

/**
 * Clear the current key sequence buffer
 * Called when mode changes or timeout occurs
 */
void keybind_clear_buffer(void);

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

/**
 * User keybindings initialization (implemented in user_hooks.c / config.c)
 * This is where users define their custom keybindings
 */
void user_keybinds_init(void);

/**
 * User text object initialization (implemented in config.c)
 * This is where users register text objects for operator composition
 */
void user_textobj_init(void);

/* Built-in keybinding callbacks live in keybinds_builtins.h */
#endif // KEYBINDS_H
