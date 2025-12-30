#include "yank.h"
#include "../hed.h"
#include "../registers.h"
#include "../buf/row.h"
#include "../ui/window.h"
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

    SizedStr result = sstr_new();

    /* Serialize based on type */
    switch (yd->type) {
        case SEL_VISUAL:
        case SEL_VISUAL_LINE:
            /* Join rows with newlines */
            for (int i = 0; i < yd->num_rows; i++) {
                sstr_append(&result, yd->rows[i].data, yd->rows[i].len);
                if (i < yd->num_rows - 1) {
                    sstr_append_char(&result, '\n');
                }
            }
            break;

        case SEL_VISUAL_BLOCK:
            /* Join rows with newlines (preserving block structure) */
            for (int i = 0; i < yd->num_rows; i++) {
                sstr_append(&result, yd->rows[i].data, yd->rows[i].len);
                if (i < yd->num_rows - 1) {
                    sstr_append_char(&result, '\n');
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
    sstr_free(&result);
    return data;
}

void yank_data_free(YankData *yd) {
    if (!yd) return;
    if (yd->rows) {
        for (int i = 0; i < yd->num_rows; i++) {
            sstr_free(&yd->rows[i]);
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
        .rows = calloc((size_t)num_rows, sizeof(SizedStr))
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
                    sstr_append(&yd.rows[idx], r->chars.data + start_col,
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
                    sstr_append(&yd.rows[idx], r->chars.data + start_col,
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
        regs_set_yank(data, len);
        free(data);
    }

    yank_data_free(&yd);
    return data ? ED_OK : ED_ERR_NOMEM;
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

    const SizedStr *reg = regs_get(reg_name);
    if (!reg || reg->len == 0) {
        return ED_OK; /* Nothing to paste */
    }

    /* Character-wise paste (no newlines) */
    if (!strchr(reg->data, '\n')) {
        if (buf->cursor.y >= buf->num_rows) {
            buf_row_insert_in(buf, buf->num_rows, "", 0);
        }
        Row *r = &buf->rows[buf->cursor.y];
        int cx = buf->cursor.x;
        if (after && cx < (int)r->chars.len) {
            cx++;
        }
        if (cx < 0) cx = 0;
        if (cx > (int)r->chars.len) cx = (int)r->chars.len;

        for (size_t k = 0; k < reg->len; k++) {
            buf_row_insert_char_in(buf, r, cx + (int)k, reg->data[k]);
        }
        buf->cursor.x = cx + (int)reg->len;
        return ED_OK;
    }

    /* Line-wise paste */
    int at = buf->cursor.y;
    if (after) {
        at = (at < buf->num_rows) ? (at + 1) : buf->num_rows;
    }

    size_t start = 0;
    size_t len = reg->len;
    int insert_row = at;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || reg->data[i] == '\n') {
            size_t seglen = i - start;
            buf_row_insert_in(buf, insert_row, reg->data + start, seglen);
            insert_row++;
            start = i + 1;
        }
    }
    buf->cursor.y = insert_row - 1;
    buf->cursor.x = 0;

    return ED_OK;
}

/* Utility */

bool yank_is_block_mode(void) {
    return last_yank_was_block;
}
