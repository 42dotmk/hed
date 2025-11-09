#include "hed.h"

/*** Cursor movement helpers ***/

void buf_cursor_move_top(void) {
    Buffer *buf = buf_cur();
    if (!buf) return;

    buf->cursor_y = 0;
    buf->cursor_x = 0;
}

void buf_cursor_move_bottom(void) {
    Buffer *buf = buf_cur();
    if (!buf) return;

    buf->cursor_y = buf->num_rows - 1;
    if (buf->cursor_y < 0) buf->cursor_y = 0;
    buf->cursor_x = 0;
}

void buf_cursor_move_line_start(void) {
    Buffer *buf = buf_cur();
    if (!buf) return;

    buf->cursor_x = 0;
}

void buf_cursor_move_line_end(void) {
    Buffer *buf = buf_cur();
    if (!buf) return;

    if (buf->cursor_y < buf->num_rows) {
        buf->cursor_x = buf->rows[buf->cursor_y].chars.len;
    }
}

void buf_cursor_move_word_forward(void) {
    Buffer *buf = buf_cur();
    if (!buf || buf->cursor_y >= buf->num_rows) return;

    Row *row = &buf->rows[buf->cursor_y];

    /* Skip current word */
    while (buf->cursor_x < (int)row->chars.len &&
           !isspace(row->chars.data[buf->cursor_x])) {
        buf->cursor_x++;
    }

    /* Skip whitespace */
    while (buf->cursor_x < (int)row->chars.len &&
           isspace(row->chars.data[buf->cursor_x])) {
        buf->cursor_x++;
    }

    /* If at end of line, move to next line */
    if (buf->cursor_x >= (int)row->chars.len &&
        buf->cursor_y < buf->num_rows - 1) {
        buf->cursor_y++;
        buf->cursor_x = 0;
    }
}

void buf_cursor_move_word_backward(void) {
    Buffer *buf = buf_cur();
    if (!buf) return;

    /* If at start of line, go to end of previous line */
    if (buf->cursor_x == 0) {
        if (buf->cursor_y > 0) {
            buf->cursor_y--;
            if (buf->cursor_y < buf->num_rows) {
                buf->cursor_x = buf->rows[buf->cursor_y].chars.len;
            }
        }
        return;
    }

    Row *row = &buf->rows[buf->cursor_y];
    buf->cursor_x--;

    /* Skip whitespace */
    while (buf->cursor_x > 0 && isspace(row->chars.data[buf->cursor_x])) {
        buf->cursor_x--;
    }

    /* Skip word */
    while (buf->cursor_x > 0 && !isspace(row->chars.data[buf->cursor_x - 1])) {
        buf->cursor_x--;
    }
}

/*** Screen positioning helpers ***/

void buf_center_screen(void) {
    Buffer *buf = buf_cur();
    if (!buf) return;

    /* Center current line in the middle of the screen */
    buf->row_offset = buf->cursor_y - (E.screen_rows / 2);
    if (buf->row_offset < 0) buf->row_offset = 0;
    if (buf->row_offset > buf->num_rows - E.screen_rows) {
        buf->row_offset = buf->num_rows - E.screen_rows;
        if (buf->row_offset < 0) buf->row_offset = 0;
    }
}

void buf_scroll_half_page_up(void) {
    Buffer *buf = buf_cur();
    if (!buf) return;

    int half_page = E.screen_rows / 2;
    buf->cursor_y -= half_page;
    if (buf->cursor_y < 0) buf->cursor_y = 0;
}

void buf_scroll_half_page_down(void) {
    Buffer *buf = buf_cur();
    if (!buf) return;

    int half_page = E.screen_rows / 2;
    buf->cursor_y += half_page;
    if (buf->cursor_y >= buf->num_rows) {
        buf->cursor_y = buf->num_rows - 1;
        if (buf->cursor_y < 0) buf->cursor_y = 0;
    }
}

void buf_scroll_page_up(void) {
    Buffer *buf = buf_cur();
    if (!buf) return;

    buf->cursor_y -= E.screen_rows;
    if (buf->cursor_y < 0) buf->cursor_y = 0;
}

void buf_scroll_page_down(void) {
    Buffer *buf = buf_cur();
    if (!buf) return;

    buf->cursor_y += E.screen_rows;
    if (buf->cursor_y >= buf->num_rows) {
        buf->cursor_y = buf->num_rows - 1;
        if (buf->cursor_y < 0) buf->cursor_y = 0;
    }
}

/*** Line operations helpers ***/

void buf_join_lines(void) {
    Buffer *buf = buf_cur();
    if (!buf || buf->cursor_y >= buf->num_rows - 1) return;

    /* Append next line to current line with a space */
    Row *current = &buf->rows[buf->cursor_y];
    Row *next = &buf->rows[buf->cursor_y + 1];

    /* Add space if current line doesn't end with one */
    if (current->chars.len > 0 &&
        current->chars.data[current->chars.len - 1] != ' ') {
        sstr_append_char(&current->chars, ' ');
    }

    /* Append next line */
    buf_row_append(current, &next->chars);

    /* Delete next line */
    buf_row_del(buf->cursor_y + 1);
}

void buf_duplicate_line(void) {
    Buffer *buf = buf_cur();
    if (!buf || buf->cursor_y >= buf->num_rows) return;

    Row *row = &buf->rows[buf->cursor_y];
    buf_row_insert(buf->cursor_y + 1, row->chars.data, row->chars.len);
    buf->cursor_y++;
}

void buf_move_line_up(void) {
    Buffer *buf = buf_cur();
    if (!buf || buf->cursor_y == 0 || buf->num_rows < 2) return;

    /* Swap with previous line */
    Row temp = buf->rows[buf->cursor_y];
    buf->rows[buf->cursor_y] = buf->rows[buf->cursor_y - 1];
    buf->rows[buf->cursor_y - 1] = temp;

    buf->cursor_y--;
}

void buf_move_line_down(void) {
    Buffer *buf = buf_cur();
    if (!buf || buf->cursor_y >= buf->num_rows - 1) return;

    /* Swap with next line */
    Row temp = buf->rows[buf->cursor_y];
    buf->rows[buf->cursor_y] = buf->rows[buf->cursor_y + 1];
    buf->rows[buf->cursor_y + 1] = temp;

    buf->cursor_y++;
}

/*** Text manipulation helpers ***/

void buf_indent_line(void) {
    Buffer *buf = buf_cur();
    if (!buf || buf->cursor_y >= buf->num_rows) return;

    Row *row = &buf->rows[buf->cursor_y];

    /* Insert TAB_STOP spaces at the beginning */
    for (int i = 0; i < TAB_STOP; i++) {
        sstr_insert_char(&row->chars, 0, ' ');
    }

    buf_row_update(row);
    buf->cursor_x += TAB_STOP;
    buf->dirty++;
}

void buf_unindent_line(void) {
    Buffer *buf = buf_cur();
    if (!buf || buf->cursor_y >= buf->num_rows) return;

    Row *row = &buf->rows[buf->cursor_y];

    /* Remove up to TAB_STOP spaces from the beginning */
    int spaces_to_remove = 0;
    for (int i = 0; i < TAB_STOP && i < (int)row->chars.len; i++) {
        if (row->chars.data[i] == ' ') {
            spaces_to_remove++;
        } else {
            break;
        }
    }

    for (int i = 0; i < spaces_to_remove; i++) {
        sstr_delete_char(&row->chars, 0);
    }

    buf_row_update(row);
    buf->cursor_x -= spaces_to_remove;
    if (buf->cursor_x < 0) buf->cursor_x = 0;
    buf->dirty++;
}

void buf_toggle_comment(void) {
    Buffer *buf = buf_cur();
    if (!buf || buf->cursor_y >= buf->num_rows) return;

    /* Determine comment string based on filetype */
    const char *comment = "// ";
    if (buf->filetype) {
        if (strcmp(buf->filetype, "python") == 0) comment = "# ";
        else if (strcmp(buf->filetype, "shell") == 0) comment = "# ";
        else if (strcmp(buf->filetype, "c") == 0) comment = "// ";
        else if (strcmp(buf->filetype, "cpp") == 0) comment = "// ";
        else if (strcmp(buf->filetype, "javascript") == 0) comment = "// ";
        else if (strcmp(buf->filetype, "rust") == 0) comment = "// ";
        else if (strcmp(buf->filetype, "go") == 0) comment = "// ";
    }

    Row *row = &buf->rows[buf->cursor_y];
    int comment_len = strlen(comment);

    /* Check if line starts with comment */
    int is_commented = 1;
    for (int i = 0; i < comment_len; i++) {
        if (i >= (int)row->chars.len || row->chars.data[i] != comment[i]) {
            is_commented = 0;
            break;
        }
    }

    if (is_commented) {
        /* Remove comment */
        for (int i = 0; i < comment_len; i++) {
            sstr_delete_char(&row->chars, 0);
        }
        buf->cursor_x -= comment_len;
        if (buf->cursor_x < 0) buf->cursor_x = 0;
    } else {
        /* Add comment */
        for (int i = comment_len - 1; i >= 0; i--) {
            sstr_insert_char(&row->chars, 0, comment[i]);
        }
        buf->cursor_x += comment_len;
    }

    buf_row_update(row);
    buf->dirty++;
}

/*** Navigation helpers ***/

void buf_goto_line(int line_num) {
    Buffer *buf = buf_cur();
    if (!buf) return;

    /* Convert from 1-indexed to 0-indexed */
    line_num--;

    if (line_num < 0) line_num = 0;
    if (line_num >= buf->num_rows) line_num = buf->num_rows - 1;

    buf->cursor_y = line_num;
    buf->cursor_x = 0;
}

void buf_find_matching_bracket(void) {
    Buffer *buf = buf_cur();
    if (!buf || buf->cursor_y >= buf->num_rows) return;

    Row *row = &buf->rows[buf->cursor_y];
    if (buf->cursor_x >= (int)row->chars.len) return;

    char ch = row->chars.data[buf->cursor_x];
    char match;
    int direction;

    /* Determine bracket type and direction */
    if (ch == '(' || ch == '{' || ch == '[') {
        direction = 1;  /* Forward */
        if (ch == '(') match = ')';
        else if (ch == '{') match = '}';
        else match = ']';
    } else if (ch == ')' || ch == '}' || ch == ']') {
        direction = -1;  /* Backward */
        if (ch == ')') match = '(';
        else if (ch == '}') match = '{';
        else match = '[';
    } else {
        return;  /* Not a bracket */
    }

    int depth = 1;
    int y = buf->cursor_y;
    int x = buf->cursor_x + direction;

    /* Search for matching bracket */
    while (y >= 0 && y < buf->num_rows) {
        row = &buf->rows[y];

        while ((direction == 1 && x < (int)row->chars.len) ||
               (direction == -1 && x >= 0)) {
            if (row->chars.data[x] == ch) {
                depth++;
            } else if (row->chars.data[x] == match) {
                depth--;
                if (depth == 0) {
                    /* Found match */
                    buf->cursor_y = y;
                    buf->cursor_x = x;
                    return;
                }
            }
            x += direction;
        }

        /* Move to next/previous line */
        y += direction;
        if (direction == 1) {
            x = 0;
        } else if (y >= 0 && y < buf->num_rows) {
            x = buf->rows[y].chars.len - 1;
        }
    }

    /* No match found */
    ed_set_status_message("No matching bracket found");
}

/*** Selection helpers ***/

void buf_select_word(void) {
    Buffer *buf = buf_cur();
    if (!buf || buf->cursor_y >= buf->num_rows) return;

    Row *row = &buf->rows[buf->cursor_y];

    /* Find word boundaries */
    int start = buf->cursor_x;
    int end = buf->cursor_x;

    /* Move start to beginning of word */
    while (start > 0 && !isspace(row->chars.data[start - 1])) {
        start--;
    }

    /* Move end to end of word */
    while (end < (int)row->chars.len && !isspace(row->chars.data[end])) {
        end++;
    }

    /* Set visual selection */
    buf->visual_start_x = start;
    buf->visual_start_y = buf->cursor_y;
    buf->cursor_x = end;
}

void buf_select_line(void) {
    Buffer *buf = buf_cur();
    if (!buf) return;

    buf->visual_start_x = 0;
    buf->visual_start_y = buf->cursor_y;

    if (buf->cursor_y < buf->num_rows) {
        buf->cursor_x = buf->rows[buf->cursor_y].chars.len;
    }
}

void buf_select_all(void) {
    Buffer *buf = buf_cur();
    if (!buf) return;

    buf->visual_start_x = 0;
    buf->visual_start_y = 0;

    buf->cursor_y = buf->num_rows - 1;
    if (buf->cursor_y < 0) buf->cursor_y = 0;

    if (buf->cursor_y < buf->num_rows) {
        buf->cursor_x = buf->rows[buf->cursor_y].chars.len;
    }
}
