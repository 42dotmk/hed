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
 *
 * Examples:
 *   keybind_register(MODE_NORMAL, "x", delete_char);
 *   keybind_register(MODE_NORMAL, "dd", delete_line);
 *   keybind_register(MODE_NORMAL, "<C-s>", save_file);
 */
void keybind_register(int mode, const char *sequence, KeybindCallback callback);

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
 * User keybindings initialization (implemented in user_hooks.c / config.c)
 * This is where users define their custom keybindings
 */
void user_keybinds_init(void);

/* Built-in keybinding callbacks live in keybinds_builtins.h */
#endif // KEYBINDS_H
