#ifndef UI_VIEW_H
#define UI_VIEW_H

/*
 * Current-window view helpers: scrolling, viewport positioning and
 * cursor navigation. These operate on buf_cur()/window_cur() and are
 * window-aware (soft wrap, gutter, screen size) — view-layer, not
 * buffer-layer.
 */

void buf_center_screen(void);         /* Center current line on screen */
void buf_scroll_half_page_up(void);   /* Scroll up half a page */
void buf_scroll_half_page_down(void); /* Scroll down half a page */

/* Go to specific line number (1-indexed) */
void buf_goto_line(int line_num);
/* Jump to matching bracket/paren */
void buf_find_matching_bracket(void);

/* Handle h/j/k/l and arrows (UTF-8 aware, wrap-aware, render-column
 * preserving on vertical moves) */
void buf_move_cursor_key(int key);

#endif
