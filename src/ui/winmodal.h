#ifndef WINMODAL_H
#define WINMODAL_H

#include "window.h"

/**
 * Modal window API
 *
 * Modal windows are special windows that:
 * - Are drawn on top of all other windows
 * - Block input to underlying windows when shown
 * - Can be positioned at custom coordinates or centered
 * - Only one modal can be shown at a time
 */

/**
 * Create a modal window
 *
 * @param x X position (column), -1 to center horizontally
 * @param y Y position (row), -1 to center vertically
 * @param width Width of the modal
 * @param height Height of the modal
 * @return Pointer to the created modal window, or NULL on failure
 */
Window *winmodal_create(int x, int y, int width, int height);

/**
 * Show a modal window
 * Makes the modal visible and blocks input to other windows
 *
 * @param modal The modal window to show
 */
void winmodal_show(Window *modal);

/**
 * Hide a modal window
 * Hides the modal and restores input to other windows
 *
 * @param modal The modal window to hide
 */
void winmodal_hide(Window *modal);

/**
 * Destroy a modal window
 * Frees all resources associated with the modal
 *
 * @param modal The modal window to destroy
 */
void winmodal_destroy(Window *modal);

/**
 * Check if a modal is currently being shown
 *
 * @return 1 if a modal is visible, 0 otherwise
 */
int winmodal_is_shown(void);

/**
 * Get the currently shown modal
 *
 * @return Pointer to the current modal, or NULL if none
 */
Window *winmodal_current(void);

/**
 * Convert the current window from layout to a modal
 * Removes the window from the layout tree and creates a centered modal
 *
 * @return Pointer to the new modal, or NULL on failure
 */
Window *winmodal_from_current(void);

/**
 * Convert a modal back to a normal window in the layout
 * Restores the window state and adds it back to the layout
 *
 * @param modal The modal window to convert back
 */
void winmodal_to_layout(Window *modal);

#endif /* WINMODAL_H */
