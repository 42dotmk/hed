#ifndef CMD_LINEOPS_H
#define CMD_LINEOPS_H

#include "buf/textobj.h"

/*
 * Line-level edit operations on the current buffer, backing the
 * :commands in cmd_edit.c. Command-layer: they act on
 * buf_cur()/window_cur() and drive undo grouping themselves.
 */

void buf_join_lines(void);     /* Join current line with next line */
void buf_duplicate_line(void); /* Duplicate current line */
void buf_move_line_up(void);   /* Swap current line with previous */
void buf_move_line_down(void); /* Swap current line with next */

void buf_indent_line(void);    /* Indent current line by TAB_STOP spaces */
void buf_unindent_line(void);  /* Remove TAB_STOP spaces from line start */
void buf_toggle_comment(void); /* Toggle line comment (based on filetype) */

/* Change = delete + enter insert mode, as one undo group */
void buf_change_selection(TextSelection *sel);
void buf_change_line(void);
void buf_change_to_line_end(void);

#endif
