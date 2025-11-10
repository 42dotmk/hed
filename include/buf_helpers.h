#ifndef BUF_HELPERS_H
#define BUF_HELPERS_H

/*
 * Buffer Helper Functions
 *
 * Common operations for cursor movement, screen positioning,
 * and buffer manipulation.
 */

/* Cursor movement helpers */
void buf_cursor_move_top(void);           /* Move cursor to first line */
void buf_cursor_move_bottom(void);        /* Move cursor to last line */
void buf_cursor_move_line_start(void);    /* Move cursor to start of line (column 0) */
void buf_cursor_move_line_end(void);      /* Move cursor to end of line */
void buf_cursor_move_word_forward(void);  /* Move cursor to next word */
void buf_cursor_move_word_backward(void); /* Move cursor to previous word */

/* Text-object deletions (prompt for delimiter, then delete around/inside) */
void buf_delete_around_char(void);
void buf_delete_inside_char(void);

/* Screen positioning helpers */
void buf_center_screen(void);             /* Center current line on screen */
void buf_scroll_half_page_up(void);       /* Scroll up half a page */
void buf_scroll_half_page_down(void);     /* Scroll down half a page */
void buf_scroll_page_up(void);            /* Scroll up one full page */
void buf_scroll_page_down(void);          /* Scroll down one full page */

/* Line operations helpers */
void buf_join_lines(void);                /* Join current line with next line */
void buf_duplicate_line(void);            /* Duplicate current line */
void buf_move_line_up(void);              /* Swap current line with previous */
void buf_move_line_down(void);            /* Swap current line with next */

/* Text manipulation helpers */
void buf_indent_line(void);               /* Indent current line by TAB_STOP spaces */
void buf_unindent_line(void);             /* Remove TAB_STOP spaces from line start */
void buf_toggle_comment(void);            /* Toggle line comment (based on filetype) */

/* Navigation helpers */
void buf_goto_line(int line_num);         /* Go to specific line number (1-indexed) */
void buf_find_matching_bracket(void);     /* Jump to matching bracket/paren */

/* Selection helpers (for VISUAL mode) */
void buf_select_word(void);               /* Select word under cursor */
void buf_select_line(void);               /* Select entire line */
void buf_select_all(void);                /* Select entire buffer */

#endif
