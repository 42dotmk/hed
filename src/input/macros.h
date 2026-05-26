#ifndef MACROS_H
#define MACROS_H

#include <stddef.h>

/**
 * Macro system - simulates keyboard input for replay functionality
 *
 * The macro queue allows injecting key sequences as if they were typed
 * by the user. This enables dot repeat, macro recording/playback, and
 * other automation features without modifying existing keybindings.
 */

/**
 * Initialize the macro queue system
 * Called during editor initialization
 */
void macro_init(void);

/**
 * Free macro queue resources
 * Called during editor cleanup
 */
void macro_free(void);

/**
 * Clear the macro queue
 * Removes all pending keys from the queue
 */
void macro_queue_clear(void);

/**
 * Check if there are keys in the macro queue
 *
 * @return 1 if keys are available, 0 if queue is empty
 */
int macro_queue_has_keys(void);

/**
 * Get the next key from the macro queue
 *
 * @return Next key code, or 0 if queue is empty
 */
int macro_queue_get_key(void);

/**
 * Replay a key sequence string
 * Parses the string and adds all keys to the macro queue
 * Supports special sequences like <Esc>, <CR>, <Tab>, <C-x>, etc.
 *
 * @param str String containing key sequence
 * @param len Length of the string
 */
void macro_replay_string(const char *str, size_t len);

/**
 * Start recording a macro to a named register (a-z)
 *
 * @param register_name Register to record to (a-z)
 */
void macro_start_recording(char register_name);

/**
 * Stop recording the current macro
 */
void macro_stop_recording(void);

/**
 * Check if currently recording a macro
 *
 * @return 1 if recording, 0 otherwise
 */
int macro_is_recording(void);

/**
 * Get the register currently being recorded to
 *
 * @return Register name (a-z) or '\0' if not recording
 */
char macro_get_recording_register(void);

/**
 * Record a keypress during macro recording
 * Converts key code to string representation and appends to register
 *
 * @param key Key code to record
 */
void macro_record_key(int key);

/**
 * Play a macro from a named register
 *
 * @param register_name Register to play from (a-z)
 */
void macro_play(char register_name);

/**
 * Play the last played macro (for @@)
 */
void macro_play_last(void);

#endif /* MACROS_H */
