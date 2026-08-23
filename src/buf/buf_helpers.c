#include "buf/buf_helpers.h"
#include "editor.h"
#include "lib/errors.h"
#include "lib/safe_string.h"
#include "lib/strutil.h"
#include <stdlib.h>
#include <string.h>

/* buf_row_insert_in/_del_in/_append_in are declared in buf_helpers.h.
 * buf_row_insert_char_in is internal-only. */
void buf_row_insert_char_in(Buffer *buf, Row *row, int at, int c);

char *buf_to_text(const Buffer *buf, size_t *out_len) {
    if (out_len)
        *out_len = 0;
    if (!buf)
        return NULL;
    size_t totlen = 0;
    for (int j = 0; j < buf->num_rows; j++)
        totlen += buf->rows[j].chars.len + 1;
    char *out = malloc(totlen + 1);
    if (!out)
        return NULL;
    char *p = out;
    for (int j = 0; j < buf->num_rows; j++) {
        memcpy(p, buf->rows[j].chars.data, buf->rows[j].chars.len);
        p += buf->rows[j].chars.len;
        *p++ = '\n';
    }
    *p = '\0';
    if (out_len)
        *out_len = totlen;
    return out;
}

void buf_append_text_lines(Buffer *buf, const char *text, size_t len) {
    if (!buf || !text)
        return;
    /* Drop trailing newline(s) so a terminating '\n' doesn't add an
     * empty final row. */
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r'))
        len--;
    size_t start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || text[i] == '\n') {
            size_t llen = i - start;
            if (llen && text[start + llen - 1] == '\r')
                llen--;
            buf_row_insert_in(buf, buf->num_rows, text + start, llen);
            start = i + 1;
        }
    }
}

EdError buf_new_scratch(const char *title, int *out_idx) {
    int idx = -1;
    EdError e = buf_new(NULL, &idx);
    if (e != ED_OK)
        return e;
    Buffer *b = &E.buffers[idx];
    free(b->filename);
    b->filename = NULL;
    if (title) {
        free(b->title);
        b->title = strdup(title);
    }
    b->dirty = 0;
    if (out_idx)
        *out_idx = idx;
    return ED_OK;
}

/*** Selection-based operations - unified interface for delete/change ***/

static void buf_delete_range(int sy, int sx, int ey, int ex) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;
    if (sy > ey || (sy == ey && sx >= ex))
        return;
    /* Capture into the delete registers first ('1'-'9' rotation +
     * unnamed). Deliberately not a yank: '0' keeps the last y so a
     * delete between two pastes doesn't lose the copied text. */
    TextSelection sel = textsel_make_range(sy, sx, ey, ex, SEL_VISUAL);
    yank_selection_as_delete(&sel);

    /* Perform deletion on the buffer */
    if (sy == ey) {
        if (sy < 0 || sy >= buf->num_rows)
            return;
        Row *row = &buf->rows[sy];
        /* shift left */
        if (ex > (int)row->chars.len)
            ex = (int)row->chars.len;
        if (sx < 0)
            sx = 0;
        if (ex < sx)
            ex = sx;
        if (ex <= sx) {
            /* Nothing left to delete after clamping (e.g. empty row,
             * whose chars.data may be NULL). */
            win->cursor.x = sx;
            return;
        }
        undo_record_replace(buf, sy);
        size_t tail = row->chars.len - ex;
        memmove(row->chars.data + sx, row->chars.data + ex, tail);
        row->chars.len -= (ex - sx);
        row->chars.data[row->chars.len] = '\0';
        buf_row_update(row);
        win->cursor.x = sx;
    } else {
        /* Delete part of first line */
        Row *first = &buf->rows[sy];
        if (sx > (int)first->chars.len)
            sx = (int)first->chars.len;
        undo_record_replace(buf, sy);
        first->chars.len = sx;
        if (first->chars.data) /* NULL when the row is empty */
            first->chars.data[sx] = '\0';
        buf_row_update(first);
        /* Delete middle lines */
        for (int y = ey - 1; y > sy; y--)
            buf_row_del_in(buf, y);
        /* Delete prefix of last line and merge */
        if (sy + 1 >= buf->num_rows) {
            win->cursor.y = sy;
            win->cursor.x = sx;
            return;
        }
        Row *last = &buf->rows[sy + 1];
        int lrx = ex;
        if (lrx < 0)
            lrx = 0;
        if (lrx > (int)last->chars.len)
            lrx = (int)last->chars.len;
        StrBuf tail =
            strbuf_from(last->chars.data + lrx, last->chars.len - lrx);
        buf_row_del_in(buf, sy + 1);
        buf_row_append_in(buf, first, &tail);
        strbuf_free(&tail);
        win->cursor.y = sy;
        win->cursor.x = sx;
    }
}

/* Delete a render-column rectangle [start_rx, end_rx_excl) on each row in
 * sy..ey. Each row's byte span is resolved independently (tab/UTF-8 aware)
 * so only the selected columns go — rows keep their content above and
 * below the block, and shorter rows are left untouched. */
void buf_delete_block(Buffer *buf, int sy, int ey, int start_rx,
                      int end_rx_excl) {
    if (!buf)
        return;
    if (sy < 0)
        sy = 0;
    if (ey >= buf->num_rows)
        ey = buf->num_rows - 1;
    /* One undo group for the whole rectangle, so a single `u` restores it. */
    undo_begin(buf, "block delete");
    for (int y = sy; y <= ey; y++) {
        Row *r = &buf->rows[y];
        int c0 = buf_row_rx_to_cx(r, start_rx);
        int c1 = buf_row_rx_to_cx(r, end_rx_excl);
        if (c0 > (int)r->chars.len)
            c0 = (int)r->chars.len;
        if (c1 > (int)r->chars.len)
            c1 = (int)r->chars.len;
        if (c1 <= c0)
            continue;
        undo_record_replace(buf, y);
        size_t tail = r->chars.len - (size_t)c1;
        memmove(r->chars.data + c0, r->chars.data + c1, tail);
        r->chars.len -= (size_t)(c1 - c0);
        r->chars.data[r->chars.len] = '\0';
        buf_row_update(r);
    }
    undo_end(buf);
    buf->dirty++;
}

/* Set (briefly) by buf_delete_selection_keep_spacing so the double-space
 * cleanup below doesn't run for change operations. */
static int suppress_space_cleanup = 0;

void buf_delete_selection(TextSelection *sel) {
    if (!sel)
        return;
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;

    /* Block selections are rectangular: never collapse the rows between
     * start and end. Delete the column span on each row instead. (The vim
     * block path resolves this via buf_delete_block directly; this guard
     * keeps any other caller from triggering a linear line-spanning delete.) */
    if (sel->type == SEL_VISUAL_BLOCK) {
        int sy = sel->start.line, ey = sel->end.line;
        if (sy > ey) {
            int t = sy;
            sy = ey;
            ey = t;
        }
        Row *r0 = (sy >= 0 && sy < buf->num_rows) ? &buf->rows[sy] : NULL;
        Row *r1 = (ey >= 0 && ey < buf->num_rows) ? &buf->rows[ey] : NULL;
        int srx = r0 ? buf_row_cx_to_rx(r0, sel->start.col) : 0;
        int erx = r1 ? buf_row_cx_to_rx(r1, sel->end.col) : sel->end.col;
        buf_delete_block(buf, sy, ey, srx, erx);
        win->cursor.y = sy;
        win->cursor.x = sel->start.col;
        return;
    }

    /* Line-wise selections remove whole rows — a multi-line V-d must
     * not fall through to the range delete below, which merges the
     * first and last row into one leftover empty line. */
    if (sel->type == SEL_VISUAL_LINE) {
        int sy = sel->start.line, ey = sel->end.line;
        if (sy > ey) {
            int t = sy;
            sy = ey;
            ey = t;
        }
        yank_selection_as_delete(sel);
        buf_delete_lines_in(buf, sy, ey);
        return;
    }

    /* Special case: whole-line deletion (dd command).
     * Pattern 1: end=(start.line+1, 0) and start.col=0  (normal lines)
     * Pattern 2: single-line selection covering full row content, col 0→len
     *            (last line, including when it is empty) */
    int del_line = sel->start.line;
    int full_line_cross = (sel->end.line == sel->start.line + 1 &&
                           sel->end.col == 0 && sel->start.col == 0);
    int full_line_last = 0;
    if (!full_line_cross && sel->start.line == sel->end.line && del_line >= 0 &&
        del_line < buf->num_rows) {
        int row_len = (int)buf->rows[del_line].chars.len;
        full_line_last = (sel->start.col == 0 && sel->end.col == row_len);
    }
    if (full_line_cross || full_line_last) {
        buf_delete_line_in(buf);
        return;
    }

    /* Normal range deletion */
    buf_delete_range(sel->start.line, sel->start.col, sel->end.line,
                     sel->end.col);

    /* Place cursor at the start of the deleted range */
    win->cursor.y = sel->start.line;
    win->cursor.x = sel->start.col;

    /* If deletion left two adjacent spaces, remove one.
     * This avoids the common double-space artifact when deleting a word
     * that had a space on each side. Suppressed for change operations
     * (ciw…): the insert happens between those spaces, so eating one
     * would glue the typed text onto the next word. */
    if (suppress_space_cleanup)
        return;
    int cy = win->cursor.y;
    int cx = win->cursor.x;
    if (cy >= 0 && cy < buf->num_rows) {
        Row *row = &buf->rows[cy];
        if (cx > 0 && cx < (int)row->chars.len && row->chars.data[cx] == ' ' &&
            row->chars.data[cx - 1] == ' ') {
            undo_record_replace(buf, cy);
            size_t tail = row->chars.len - (cx + 1);
            memmove(row->chars.data + cx, row->chars.data + cx + 1, tail);
            row->chars.len--;
            row->chars.data[row->chars.len] = '\0';
            buf_row_update(row);
        }
    }
}

/* Deletion that skips the double-space cleanup: for change (ciw…) and
 * paste-over-selection, where new text lands between the two spaces
 * the cleanup would otherwise collapse. */
void buf_delete_selection_keep_spacing(TextSelection *sel) {
    suppress_space_cleanup = 1;
    buf_delete_selection(sel);
    suppress_space_cleanup = 0;
}

/* Yank data insertion */

EdError buf_insert_yank_data(Buffer *buf, int at_line, int at_col,
                             const YankData *yd, bool after) {
    if (!buf || !yd || yd->num_rows == 0 || !yd->rows) {
        return ED_ERR_INVALID_ARG;
    }

    if (buf->readonly) {
        ed_set_status_message("Buffer is read-only");
        return ED_ERR_BUFFER_READONLY;
    }

    /* Ensure we have at least one line */
    if (buf->num_rows == 0) {
        buf_row_insert_in(buf, 0, "", 0);
    }

    /* Clamp line to valid range */
    if (at_line < 0)
        at_line = 0;
    if (at_line >= buf->num_rows)
        at_line = buf->num_rows - 1;

    switch (yd->type) {
    case SEL_VISUAL: {
        /* Character-wise paste: inline at cursor */
        Row *r = &buf->rows[at_line];
        int insert_col = at_col;

        if (after && insert_col < (int)r->chars.len) {
            insert_col++;
        }
        if (insert_col < 0)
            insert_col = 0;
        if (insert_col > (int)r->chars.len)
            insert_col = (int)r->chars.len;

        if (yd->num_rows == 1) {
            /* Single line: insert inline */
            for (size_t i = 0; i < yd->rows[0].len; i++) {
                buf_row_insert_char_in(buf, r, insert_col + (int)i,
                                       yd->rows[0].data[i]);
            }
        } else {
            /* Multiple lines: split current line and insert between */
            /* Save text after cursor */
            undo_record_replace(buf, at_line);
            StrBuf rest = strbuf_new();
            if (insert_col < (int)r->chars.len) {
                strbuf_append(&rest, r->chars.data + insert_col,
                              r->chars.len - (size_t)insert_col);
                r->chars.len = (size_t)insert_col;
            }

            /* Append first yank row to current line */
            strbuf_append(&r->chars, yd->rows[0].data, yd->rows[0].len);
            buf_row_update(r);

            /* Insert middle rows as new lines */
            for (int i = 1; i < yd->num_rows - 1; i++) {
                buf_row_insert_in(buf, at_line + i, yd->rows[i].data,
                                  yd->rows[i].len);
            }

            /* Insert last row with saved rest */
            int last_idx = at_line + yd->num_rows - 1;
            StrBuf last_line = strbuf_new();
            strbuf_append(&last_line, yd->rows[yd->num_rows - 1].data,
                          yd->rows[yd->num_rows - 1].len);
            strbuf_append(&last_line, rest.data, rest.len);
            buf_row_insert_in(buf, last_idx, last_line.data, last_line.len);

            strbuf_free(&last_line);
            strbuf_free(&rest);
        }
        break;
    }

    case SEL_VISUAL_LINE: {
        /* Line-wise paste: insert as full lines */
        int insert_line = after ? (at_line + 1) : at_line;
        if (insert_line > buf->num_rows)
            insert_line = buf->num_rows;

        for (int i = 0; i < yd->num_rows; i++) {
            buf_row_insert_in(buf, insert_line + i, yd->rows[i].data,
                              yd->rows[i].len);
        }
        break;
    }

    case SEL_VISUAL_BLOCK: {
        /* Block-wise paste: insert block at column */
        int insert_col = after ? (at_col + 1) : at_col;
        if (insert_col < 0)
            insert_col = 0;

        for (int i = 0; i < yd->num_rows; i++) {
            int target_line = at_line + i;

            /* Ensure we have enough lines */
            while (target_line >= buf->num_rows) {
                buf_row_insert_in(buf, buf->num_rows, "", 0);
            }

            Row *r = &buf->rows[target_line];

            /* Pad line with spaces if needed */
            if ((int)r->chars.len < insert_col) {
                undo_record_replace(buf, target_line);
                while ((int)r->chars.len < insert_col)
                    strbuf_append_char(&r->chars, ' ');
            }

            /* Insert the block segment */
            for (size_t j = 0; j < yd->rows[i].len; j++) {
                buf_row_insert_char_in(buf, r, insert_col + (int)j,
                                       yd->rows[i].data[j]);
            }
        }
        break;
    }

    case SEL_NONE:
        return ED_ERR_INVALID_ARG;
    }

    buf->dirty++;
    return ED_OK;
}
