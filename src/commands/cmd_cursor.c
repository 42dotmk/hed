#include "hed.h"

/* Add a cursor at the current position */
void cmd_cursor_add(const char *args) {
	BUF(buf)
    Cursor c = buf->cursor;
    vec_push(&buf->cursors, Cursor, c);
    ed_set_status_message("Added cursor (%d cursors total)", (int)buf->cursors.len);
    ed_mark_dirty();
}

/* Clear all extra cursors, keep only primary */
void cmd_cursor_clear(const char *args) {
	BUF(buf)
    if (buf->cursors.len == 0) {
        ed_set_status_message("No extra cursors to clear");
        return;
    }

    vec_free(&buf->cursors, Cursor);
    buf->cursors = (CursorVec){0};

    ed_set_status_message("Cleared all extra cursors");
    ed_mark_dirty();
}

/* Add cursor on line below */
void cmd_cursor_add_below(const char *args) {
	BUF(buf)
    if (buf->cursor.y >= buf->num_rows - 1) {
        ed_set_status_message("Already on last line");
        return;
    }

    /* Add cursor at same x position, next line */
    Cursor c = {buf->cursor.x, buf->cursor.y + 1};

    /* Clamp x to line length */
    if (c.y < buf->num_rows) {
        int line_len = (int)buf->rows[c.y].chars.len;
        if (c.x > line_len)
            c.x = line_len;
    }

    vec_push(&buf->cursors, Cursor, c);
    ed_set_status_message("Added cursor below (%d cursors)", (int)buf->cursors.len);
    ed_mark_dirty();
}

/* Add cursor on line above */
void cmd_cursor_add_above(const char *args) {
	BUF(buf)

    if (buf->cursor.y <= 0) {
        ed_set_status_message("Already on first line");
        return;
    }

    /* Add cursor at same x position, previous line */
    Cursor c = {buf->cursor.x, buf->cursor.y - 1};

    /* Clamp x to line length */
    if (c.y >= 0 && c.y < buf->num_rows) {
        int line_len = (int)buf->rows[c.y].chars.len;
        if (c.x > line_len)
            c.x = line_len;
    }

    vec_push(&buf->cursors, Cursor, c);
    ed_set_status_message("Added cursor above (%d cursors)", (int)buf->cursors.len);
    ed_mark_dirty();
}
