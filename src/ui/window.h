#ifndef WINDOW_H
#define WINDOW_H

/*
 * WINDOW OWNERSHIP AND LIFECYCLE DOCUMENTATION
 * ============================================
 *
 * Memory Ownership Rules:
 * ----------------------
 * 1. Window Storage:
 *    - All windows live in the growable vector E.windows (WindowVec)
 *    - Windows are stored in a dynamically-sized array that grows as needed
 *    - No fixed limit on number of windows (previously MAX_WINDOWS = 8)
 *    - When a window is closed, it's removed and remaining windows are shifted
 *
 * 2. Window-Buffer Relationship:
 *    - Windows reference buffers by INDEX (buffer_index), NOT by pointer
 *    - This is critical because buffer indices can change when buffers are closed
 *    - Multiple windows can reference the same buffer (split views)
 *    - A window does NOT own its buffer; buffers are independent
 *
 * 3. Cursor State:
 *    - Each window has its own cursor position
 *    - These are window-local coordinates within the buffer
 *    - Cursor positions are independent per window, even for the same buffer
 *
 * 4. Layout Tree:
 *    - Windows are organized in a layout tree (E.wlayout_root)
 *    - The tree defines the geometry and split arrangement
 *    - Layout nodes reference windows by index (leaf_index)
 *    - See wlayout.h for layout tree documentation
 *
 * Window Lifecycle:
 * ----------------
 * 1. Initialization: windows_init()
 *    - Creates a single full-screen window at startup
 *    - Initializes layout tree with one leaf node
 *
 * 2. Splitting: windows_split_vertical() or windows_split_horizontal()
 *    - Creates a new window by duplicating the current one
 *    - Updates layout tree to reflect the split
 *    - New window references the same buffer as the original
 *    - Focus moves to the new window
 *
 * 3. Focusing: windows_focus_next()
 *    - Cycles focus between windows
 *    - Updates E.current_window and E.current_buffer
 *    - Only one window has focus=1 at any time
 *
 * 4. Closing: windows_close_current()
 *    - Removes the focused window
 *    - Shifts remaining windows down in the array
 *    - Updates layout tree and reindexes leaf nodes
 *    - Focus moves to the next available window
 *    - IMPORTANT: Cannot close the last window
 *
 * Buffer Index Invalidation:
 * -------------------------
 * When a buffer is closed, all buffer indices > closed_index are decremented.
 * Windows automatically update their buffer_index to reflect this change.
 * This is why windows MUST use indices, not pointers.
 *
 * Example:
 *   - Window A: buffer_index = 2
 *   - Window B: buffer_index = 4
 *   - buf_close(3) is called
 *   - After: Window A: buffer_index = 2 (unchanged)
 *   - After: Window B: buffer_index = 3 (decremented because > 3)
 *
 * Thread Safety:
 * -------------
 * - The window system is NOT thread-safe
 * - All window operations must occur on the main thread
 * - No concurrent access is supported
 *
 * Common Pitfalls:
 * ---------------
 * - DON'T: Store Window* pointers across operations that might close windows
 * - DON'T: Access window.buffer_index without bounds checking
 * - DON'T: Assume buffer_index is stable across buf_close() operations
 * - DO: Use window_cur() to safely get the current window
 * - DO: Use E.buffers[win->buffer_index] with bounds checking
 * - DO: Revalidate buffer_index after any buffer operations
 */

#include "buffer.h"

typedef struct {
    int top;            /* 1-based row on the terminal */
    int left;           /* 1-based column on the terminal */
    int height;         /* number of content rows */
    int width;          /* number of columns */
    int buffer_index;   /* index into E.buffers */
    int focus;          /* 1 if focused */
    int is_quickfix;    /* 1 if this window is a quickfix pane */
    int wrap;           /* 1 if soft-wrap is enabled */
    int row_offset;     /* first visible visual row (wrap-aware) */
    int col_offset;     /* first visible buffer column (render x) */
    Cursor cursor;      /* window-local cursor (x=col, y=row index) */
    /* Gutter configuration: 0=off, 1=auto line numbers, 2=fixed width */
    int gutter_mode;
    int gutter_fixed_width;
} Window;


/* Window management */
void windows_init(void);
Window *window_cur(void);
/* Attach a buffer to a window and sync editor focus if needed */
void win_attach_buf(Window *win, Buffer *buf);

/* Simple 2-way splits */
void windows_split_vertical(void);   /* side-by-side */
void windows_split_horizontal(void); /* stacked */
void windows_focus_next(void);       /* cycle focus */
void windows_close_current(void);    /* close focused window */

#endif /* WINDOW_H */
