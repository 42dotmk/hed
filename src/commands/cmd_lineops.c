#include "commands/cmd_lineops.h"
#include "buf/buf_helpers.h"
#include "editor.h"
#include "lib/errors.h"
#include "lib/safe_string.h"
#include <string.h>

void buf_join_lines(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win) ||
        win->cursor.y >= buf->num_rows - 1)
        return;

    int y = win->cursor.y;
    Row *current = &buf->rows[y];
    Row *next = &buf->rows[y + 1];
    if (!PTR_VALID(current) || !PTR_VALID(next))
        return;

    /* Vim J: the joined line's leading indentation is dropped and the
     * two lines are separated by a single space (none when the current
     * line already ends in whitespace or the next line is blank). */
    const char *nd = next->chars.data;
    size_t nlen = next->chars.len;
    size_t skip = 0;
    while (skip < nlen && (nd[skip] == ' ' || nd[skip] == '\t'))
        skip++;

    char last = current->chars.len > 0
                    ? current->chars.data[current->chars.len - 1]
                    : '\0';
    int need_space =
        (current->chars.len > 0 && last != ' ' && last != '\t' && skip < nlen);

    /* Capture original 'current' row before any direct char mutation. */
    undo_record_replace(buf, y);

    int join_x = (int)current->chars.len;
    if (need_space)
        strbuf_append_char(&current->chars, ' ');
    if (skip < nlen)
        strbuf_append(&current->chars, nd + skip, nlen - skip);
    buf_row_update(current);

    /* Delete the (untrimmed) next row so undo restores it verbatim. */
    buf_row_del_in(buf, y + 1);
    buf->dirty++;

    /* Cursor on the join point, like Vim. */
    if (join_x > 0 && join_x >= (int)current->chars.len)
        join_x = (int)current->chars.len - 1;
    win->cursor.x = join_x;
}

void buf_duplicate_line(void) {
    BUF(buf)
    buf_yank_line_in(buf);
    paste_from_register(buf, '"', true);
}

void buf_move_line_up(void) {
    ASSERT_EDIT(buf, win);
    if (win->cursor.y <= 0)
        return;
    undo_begin(buf, "move line up");
    undo_record_replace(buf, win->cursor.y - 1);
    undo_record_replace(buf, win->cursor.y);
    Row temp = buf->rows[win->cursor.y];
    buf->rows[win->cursor.y] = buf->rows[win->cursor.y - 1];
    buf->rows[win->cursor.y - 1] = temp;
    win->cursor.y--;
    undo_end(buf);
    buf->dirty++;
}

void buf_move_line_down(void) {
    ASSERT_EDIT(buf, win)
    if (win->cursor.y >= buf->num_rows - 1)
        return;
    undo_begin(buf, "move line down");
    undo_record_replace(buf, win->cursor.y);
    undo_record_replace(buf, win->cursor.y + 1);
    Row temp = buf->rows[win->cursor.y];
    buf->rows[win->cursor.y] = buf->rows[win->cursor.y + 1];
    buf->rows[win->cursor.y + 1] = temp;
    win->cursor.y++;
    undo_end(buf);
    buf->dirty++;
}

void buf_indent_line(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win) ||
        !BOUNDS_CHECK(win->cursor.y, buf->num_rows))
        return;
    if (buf->readonly) {
        ed_set_status_message("Buffer is read-only");
        return;
    }

    Row *row = &buf->rows[win->cursor.y];

    undo_record_replace(buf, win->cursor.y);

    /* Insert TAB_STOP spaces at the beginning */
    for (int i = 0; i < TAB_STOP; i++) {
        strbuf_insert_char(&row->chars, 0, ' ');
    }

    buf_row_update(row);
    win->cursor.x += TAB_STOP;
    buf->dirty++;
}

void buf_unindent_line(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win) ||
        !BOUNDS_CHECK(win->cursor.y, buf->num_rows))
        return;
    if (buf->readonly) {
        ed_set_status_message("Buffer is read-only");
        return;
    }

    Row *row = &buf->rows[win->cursor.y];

    undo_record_replace(buf, win->cursor.y);

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
        strbuf_delete_char(&row->chars, 0);
    }

    buf_row_update(row);
    win->cursor.x -= spaces_to_remove;
    if (win->cursor.x < 0)
        win->cursor.x = 0;
    buf->dirty++;
}

void buf_toggle_comment(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win) ||
        !BOUNDS_CHECK(win->cursor.y, buf->num_rows))
        return;
    if (buf->readonly) {
        ed_set_status_message("Buffer is read-only");
        return;
    }

    /* Determine comment string based on filetype */
    const char *comment = "// ";
    if (buf->filetype) {
        if (strcmp(buf->filetype, "python") == 0)
            comment = "# ";
        else if (strcmp(buf->filetype, "shell") == 0)
            comment = "# ";
        else if (strcmp(buf->filetype, "c") == 0)
            comment = "// ";
        else if (strcmp(buf->filetype, "cpp") == 0)
            comment = "// ";
        else if (strcmp(buf->filetype, "javascript") == 0)
            comment = "// ";
        else if (strcmp(buf->filetype, "rust") == 0)
            comment = "// ";
        else if (strcmp(buf->filetype, "go") == 0)
            comment = "// ";
    }

    int y = win->cursor.y;
    Row *row = &buf->rows[y];
    int comment_len = strlen(comment);

    undo_record_replace(buf, y);

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
            strbuf_delete_char(&row->chars, 0);
        }
        win->cursor.x -= comment_len;
        if (win->cursor.x < 0)
            win->cursor.x = 0;
    } else {
        for (int i = comment_len - 1; i >= 0; i--) {
            strbuf_insert_char(&row->chars, 0, comment[i]);
        }
        win->cursor.x += comment_len;
    }

    buf_row_update(row);
    buf->dirty++;
}

void buf_change_selection(TextSelection *sel) {
    Buffer *buf = buf_cur();
    /* Open one group spanning the deletion AND the upcoming insert
     * session so c<motion><text><Esc> is a single undo step. The mode
     * change hook will see an open group on INSERT entry and leave it
     * alone; Esc closes it. */
    if (buf)
        undo_begin(buf, "change");
    buf_delete_selection_keep_spacing(sel);
    ed_set_mode(MODE_INSERT);
}

void buf_change_line(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;

    TextSelection sel;
    if (!textobj_line(buf, win->cursor.y, win->cursor.x, &sel))
        return;

    buf_change_selection(&sel);
    win->cursor.x = 0;
}

void buf_change_to_line_end(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;

    TextSelection sel;
    if (!textobj_to_line_end(buf, win->cursor.y, win->cursor.x, &sel))
        return;

    buf_change_selection(&sel);
    win->cursor.x = sel.start.col;
}
