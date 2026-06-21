#include "utils/yank.h"
#include "editor.h"
#include "buf/buf_helpers.h"
#include "input/registers.h"
#include "buf/row.h"
#include "ui/window.h"
#include <stdlib.h>
#include <string.h>

/* Forward declarations of internal buffer functions */
void buf_row_insert_in(Buffer *buf, int at, const char *s, size_t len);
void buf_row_del_in(Buffer *buf, int at);
void buf_row_insert_char_in(Buffer *buf, Row *row, int at, int c);

/* Track if last yank was block mode */
static bool last_yank_was_block = false;

/* Helper: Convert YankData to string (for register storage) */
static char *yank_data_to_string(const YankData *yd, size_t *out_len) {
    if (!yd || yd->num_rows == 0 || !yd->rows) {
        *out_len = 0;
        return NULL;
    }

    StrBuf result = strbuf_new();

    /* Serialize based on type */
    switch (yd->type) {
        case SEL_VISUAL:
        case SEL_VISUAL_LINE:
            /* Join rows with newlines */
            for (int i = 0; i < yd->num_rows; i++) {
                strbuf_append(&result, yd->rows[i].data, yd->rows[i].len);
                if (i < yd->num_rows - 1) {
                    strbuf_append_char(&result, '\n');
                }
            }
            /* Trailing newline marks the yank as line-wise so paste
             * inserts as new lines (matches vim yy/p semantics). */
            if (yd->type == SEL_VISUAL_LINE) {
                strbuf_append_char(&result, '\n');
            }
            break;

        case SEL_VISUAL_BLOCK:
            /* Join rows with newlines (preserving block structure) */
            for (int i = 0; i < yd->num_rows; i++) {
                strbuf_append(&result, yd->rows[i].data, yd->rows[i].len);
                if (i < yd->num_rows - 1) {
                    strbuf_append_char(&result, '\n');
                }
            }
            break;

        case SEL_NONE:
            *out_len = 0;
            return NULL;
    }

    *out_len = result.len;
    char *data = malloc(result.len + 1);
    if (data) {
        memcpy(data, result.data, result.len);
        data[result.len] = '\0';
    }
    strbuf_free(&result);
    return data;
}

void yank_data_free(YankData *yd) {
    if (!yd) return;
    if (yd->rows) {
        for (int i = 0; i < yd->num_rows; i++) {
            strbuf_free(&yd->rows[i]);
        }
        free(yd->rows);
    }
    yd->rows = NULL;
    yd->num_rows = 0;
}

YankData yank_data_new(Buffer *buf, const TextSelection *sel) {
    if (!buf || !sel) {
        return (YankData){0};
    }

    int sy = sel->start.line;
    int sx = sel->start.col;
    int ey = sel->end.line;
    int ex = sel->end.col;

    /* Validate bounds */
    if (sy < 0 || sy >= buf->num_rows || ey < 0 || ey >= buf->num_rows) {
        return (YankData){0};
    }

    int num_rows = ey - sy + 1;
    YankData yd = {
        .type = sel->type,
        .num_rows = num_rows,
        .rows = calloc((size_t)num_rows, sizeof(StrBuf))
    };

    switch (sel->type) {
        case SEL_VISUAL:
        case SEL_VISUAL_LINE:
            /* Character-wise or line-wise: store text with newlines */
            for (int y = sy; y <= ey; y++) {
                if (y >= buf->num_rows) break;
                Row *r = &buf->rows[y];
                int start_col = (y == sy) ? sx : 0;
                int end_col = (y == ey) ? ex : (int)r->chars.len;

                if (start_col < 0) start_col = 0;
                if (end_col > (int)r->chars.len) end_col = (int)r->chars.len;

                if (end_col > start_col) {
                    int idx = y - sy;
                    strbuf_append(&yd.rows[idx], r->chars.data + start_col,
                               (size_t)(end_col - start_col));
                }
            }
            break;

        case SEL_VISUAL_BLOCK:
            /* Block-wise: store each row segment separately */
            for (int y = sy; y <= ey; y++) {
                if (y >= buf->num_rows) break;
                Row *r = &buf->rows[y];

                /* For block mode, sx and ex are the column boundaries */
                int start_col = sx;
                int end_col = ex;

                if (start_col < 0) start_col = 0;
                if (end_col > (int)r->chars.len) end_col = (int)r->chars.len;

                int idx = y - sy;
                if (end_col > start_col) {
                    strbuf_append(&yd.rows[idx], r->chars.data + start_col,
                               (size_t)(end_col - start_col));
                }
                /* For block mode, rows can be empty if the line is shorter */
            }
            break;

        case SEL_NONE:
            /* Empty selection */
            break;
    }

    return yd;
}

EdError yank_selection(const TextSelection *sel) {
	BUF(buf)
    if (!sel) {
        return ED_ERR_INVALID_ARG;
    }

    /* Extract as YankData */
    YankData yd = yank_data_new(buf, sel);
    if (!yd.rows) {
        return ED_ERR_NOMEM;
    }

    /* Convert to string for register storage */
    size_t len;
    char *data = yank_data_to_string(&yd, &len);

    last_yank_was_block = (yd.type == SEL_VISUAL_BLOCK);

    if (data) {
        RegType rt = yd.type == SEL_VISUAL_LINE  ? REG_LINEWISE
                   : yd.type == SEL_VISUAL_BLOCK ? REG_BLOCKWISE
                                                 : REG_CHARWISE;
        regs_set_yank_typed(data, len, rt);
        free(data);
    }

    yank_data_free(&yd);
    return data ? ED_OK : ED_ERR_NOMEM;
}

/* Block-wise yank straight from a render-column rectangle. Each row's
 * byte span is resolved independently (tab/UTF-8 aware) via rx->cx, so
 * the rectangle is honoured even when rows differ in width. Rows that
 * don't reach the block are stored empty, preserving the shape. */
EdError yank_block(Buffer *buf, int sy, int ey, int start_rx, int end_rx_excl) {
    if (!buf) return ED_ERR_INVALID_ARG;
    if (sy < 0) sy = 0;
    if (ey >= buf->num_rows) ey = buf->num_rows - 1;
    if (sy > ey) return ED_ERR_INVALID_ARG;

    StrBuf out = strbuf_new();
    for (int y = sy; y <= ey; y++) {
        Row *r = &buf->rows[y];
        int c0 = buf_row_rx_to_cx(r, start_rx);
        int c1 = buf_row_rx_to_cx(r, end_rx_excl);
        if (c0 > (int)r->chars.len) c0 = (int)r->chars.len;
        if (c1 > (int)r->chars.len) c1 = (int)r->chars.len;
        if (c1 > c0)
            strbuf_append(&out, r->chars.data + c0, (size_t)(c1 - c0));
        if (y < ey)
            strbuf_append_char(&out, '\n');
    }

    last_yank_was_block = true;
    regs_set_yank_typed(out.data, out.len, REG_BLOCKWISE);
    strbuf_free(&out);
    return ED_OK;
}

EdError paste_from_register(Buffer *buf, char reg_name, bool after) {
    WIN(win)
    if (!buf || !win) {
        return ED_ERR_INVALID_ARG;
    }

    if (buf->readonly) {
        ed_set_status_message("Buffer is read-only");
        return ED_ERR_BUFFER_READONLY;
    }

    const StrBuf *reg = regs_get(reg_name);
    if (!reg || reg->len == 0) {
        return ED_OK; /* Nothing to paste */
    }

    /* Decide the paste shape from the register's recorded type, falling
     * back to charwise. Linewise content may carry a trailing newline
     * (visual-line yank) or not (dd) — the type, not the newline, is the
     * source of truth, so we strip at most one trailing newline. */
    RegType rt = regs_get_type(reg_name);
    SelectionType st = rt == REG_LINEWISE  ? SEL_VISUAL_LINE
                     : rt == REG_BLOCKWISE ? SEL_VISUAL_BLOCK
                                           : SEL_VISUAL;

    size_t len = reg->len;
    if (st == SEL_VISUAL_LINE && len > 0 && reg->data[len - 1] == '\n')
        len--;

    /* Split the register text into one StrBuf per line. */
    int num_rows = 1;
    for (size_t i = 0; i < len; i++)
        if (reg->data[i] == '\n') num_rows++;

    YankData yd = {
        .type = st,
        .num_rows = num_rows,
        .rows = calloc((size_t)num_rows, sizeof(StrBuf)),
    };
    if (!yd.rows) return ED_ERR_NOMEM;

    size_t start = 0;
    int idx = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || reg->data[i] == '\n') {
            yd.rows[idx++] = strbuf_from(reg->data + start, i - start);
            start = i + 1;
        }
    }

    int at_line = win->cursor.y;
    int at_col = win->cursor.x;
    EdError err = buf_insert_yank_data(buf, at_line, at_col, &yd, after);

    /* Cursor placement, roughly matching Vim:
     *  - linewise: start of the first pasted line
     *  - blockwise: the insert column on the first row
     *  - charwise: on the last character of the pasted text */
    if (err == ED_OK) {
        if (st == SEL_VISUAL_LINE) {
            int line = after ? at_line + 1 : at_line;
            if (line >= buf->num_rows) line = buf->num_rows - 1;
            if (line < 0) line = 0;
            win->cursor.y = line;
            win->cursor.x = 0;
        } else {
            int rowlen = (at_line < buf->num_rows)
                             ? (int)buf->rows[at_line].chars.len : 0;
            int col = at_col;
            if (after && col < rowlen) col++;
            if (col < 0) col = 0;
            win->cursor.x = col;
            if (st == SEL_VISUAL && num_rows == 1 && yd.rows[0].len > 0)
                win->cursor.x = col + (int)yd.rows[0].len - 1;
        }
    }

    yank_data_free(&yd);
    return err;
}

/* Utility */

bool yank_is_block_mode(void) {
    return last_yank_was_block;
}
