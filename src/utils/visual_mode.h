#ifndef VISUAL_MODE_H
#define VISUAL_MODE_H

/*
 * VISUAL MODE - Complete Rewrite
 * ================================
 *
 * Character-wise visual selection with proper operations.
 *
 * Design:
 * - Visual selection anchor stored in Window (visual_start_x/y)
 * - Current cursor position is the selection endpoint
 * - Selection is inclusive (includes character under cursor)
 * - Operations work on the selected range
 *
 * Supported Operations:
 * - y: yank (copy) selection
 * - d/x: delete selection
 * - c: change selection (delete + enter insert mode)
 * - >: indent selection
 * - <: unindent selection
 * - ~: toggle case
 * - u: lowercase
 * - U: uppercase
 *
 * Entry:
 * - v: Enter visual mode (sets anchor at cursor)
 *
 * Exit:
 * - Esc: Exit to normal mode
 * - Most operations: Execute and return to normal mode
 *
 * Movement:
 * - h/j/k/l, arrows, w/b, etc: Extend selection
 */

/* Get normalized selection range (ensures start <= end) */
int visual_get_range(int *start_y, int *start_x, int *end_y, int *end_x);

/* Visual mode operations */
void visual_yank(void);    /* Copy selection to clipboard */
void visual_delete(void);  /* Delete selection */
void visual_change(void);  /* Delete selection and enter insert mode */
void visual_indent(void);  /* Indent selected lines */
void visual_unindent(void);/* Unindent selected lines */
void visual_toggle_case(void); /* Toggle case of selection */
void visual_lowercase(void);   /* Convert selection to lowercase */
void visual_uppercase(void);   /* Convert selection to UPPERCASE */

/* Visual mode keypress handler (replaces handle_visual_mode_keypress) */
void visual_mode_keypress(int c);

#endif /* VISUAL_MODE_H */
