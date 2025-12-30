#ifndef AUTOCOMPLETE_H
#define AUTOCOMPLETE_H

/**
 * Autocomplete system using modal windows
 *
 * Displays completion suggestions in a modal window below the cursor
 * Triggered with <C-n> in insert mode
 */

/**
 * Initialize the autocomplete system
 * Should be called once at editor startup
 */
void autocomplete_init(void);

/**
 * Trigger autocomplete - shows if inactive, navigates next if active
 * This is the main function bound to <C-n>
 */
void autocomplete_trigger(void);

/**
 * Show autocompletion suggestions for the current word
 * Creates a modal window with matching words from the buffer
 * Positioned below the cursor
 */
void autocomplete_show(void);

/**
 * Hide the autocomplete modal and cleanup
 */
void autocomplete_hide(void);

/**
 * Check if autocomplete is currently active
 * @return 1 if active, 0 otherwise
 */
int autocomplete_is_active(void);

/**
 * Navigate to the next completion
 */
void autocomplete_next(void);

/**
 * Navigate to the previous completion
 */
void autocomplete_prev(void);

/**
 * Accept the currently selected completion
 * Replaces the partial word with the selected completion
 */
void autocomplete_accept(void);

/**
 * Cancel autocomplete and hide the modal
 */
void autocomplete_cancel(void);

#endif /* AUTOCOMPLETE_H */
