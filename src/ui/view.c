#include "ui/view.h"
#include "buf/buf_helpers.h"
#include "editor.h"
#include "lib/errors.h"
#include "lib/safe_string.h"
#include "lib/strutil.h"
#include <stdlib.h>

static void cur_sync_from_window(Buffer *buf, Window *win) {
    if (PTR_VALID(buf) && PTR_VALID(win) && PTR_VALID(buf->cursor)) {
        buf->cursor->x = win->cursor.x;
        buf->cursor->y = win->cursor.y;
    }
}
static void cur_sync_to_window(Buffer *buf, Window *win) {
    if (PTR_VALID(buf) && PTR_VALID(win) && PTR_VALID(buf->cursor)) {
        win->cursor.x = buf->cursor->x;
        win->cursor.y = buf->cursor->y;
    }
}

/* Visual-line helpers for soft-wrap navigation */
static int row_visual_height_buf(const Buffer *buf, int row_index,
                                 int content_cols, int wrap) {
    if (!wrap)
        return 1;
    if (!buf)
        return 1;
    if (row_index < 0 || row_index >= buf->num_rows)
        return 1;
    if (content_cols <= 0)
        return 1;
    const Row *row = &buf->rows[row_index];
    int rcols = buf_row_cx_to_rx(row, (int)row->chars.len);
    if (rcols <= 0)
        return 1;
    int h = (rcols + content_cols - 1) / content_cols;
    return h < 1 ? 1 : h;
}

static int cursor_visual_position(const Buffer *buf, const Window *win,
                                  int content_cols, int *out_vis_col) {
    if (!buf || !win || buf->num_rows <= 0) {
        if (out_vis_col)
            *out_vis_col = 0;
        return 0;
    }
    int cy = win->cursor.y;
    if (cy < 0)
        cy = 0;
    if (cy >= buf->num_rows)
        cy = buf->num_rows - 1;

    int visual = 0;
    for (int y = 0; y < cy; y++) {
        visual += row_visual_height_buf(buf, y, content_cols, 1);
    }
    const Row *row = &buf->rows[cy];
    int rx = buf_row_cx_to_rx(row, win->cursor.x);
    if (rx < 0)
        rx = 0;
    int h = row_visual_height_buf(buf, cy, content_cols, 1);
    int seg = rx / content_cols;
    if (seg >= h)
        seg = h - 1;
    int vis_col = rx % content_cols;
    if (out_vis_col)
        *out_vis_col = vis_col;
    return visual + seg;
}

static int buffer_total_visual_rows(const Buffer *buf, int content_cols) {
    if (!buf || buf->num_rows <= 0)
        return 0;
    int total = 0;
    for (int y = 0; y < buf->num_rows; y++) {
        total += row_visual_height_buf(buf, y, content_cols, 1);
    }
    return total;
}

static void cursor_from_visual(Buffer *buf, Window *win, int target_visual,
                               int content_cols, int vis_col) {
    if (!buf || !win || buf->num_rows <= 0) {
        win->cursor.y = 0;
        win->cursor.x = 0;
        return;
    }
    if (target_visual < 0)
        target_visual = 0;

    int y = 0;
    while (y < buf->num_rows) {
        int h = row_visual_height_buf(buf, y, content_cols, 1);
        if (target_visual < h)
            break;
        target_visual -= h;
        y++;
    }
    if (y >= buf->num_rows) {
        y = buf->num_rows - 1;
        if (y < 0) {
            win->cursor.y = 0;
            win->cursor.x = 0;
            return;
        }
        int h = row_visual_height_buf(buf, y, content_cols, 1);
        target_visual = h - 1;
        if (target_visual < 0)
            target_visual = 0;
    }

    Row *row = &buf->rows[y];
    int rcols = buf_row_cx_to_rx(row, (int)row->chars.len);
    if (rcols < 0)
        rcols = 0;
    if (content_cols <= 0)
        content_cols = 1;

    int seg_start = target_visual * content_cols;
    if (seg_start > rcols) {
        seg_start = (rcols / content_cols) * content_cols;
    }
    int rx = seg_start + vis_col;
    int seg_end = seg_start + content_cols;
    if (rx >= seg_end)
        rx = seg_end - 1;
    if (rx >= rcols && rcols > 0)
        rx = rcols - 1;
    if (rcols == 0)
        rx = 0;

    int cx = buf_row_rx_to_cx(row, rx);
    win->cursor.y = y;
    win->cursor.x = cx;
}

/*** Screen positioning ***/

void buf_center_screen(void) {
    BUFWIN(buf, win);
    if (!PTR_VALID(win))
        return;
    /* Center current line in the middle of the current window (logical rows) */
    if (!buf || win->wrap)
        return;
    win->row_offset = win->cursor.y - (win->height / 2);
    if (win->row_offset < 0)
        win->row_offset = 0;
    int maxoff = buf->num_rows - win->height;
    if (win->row_offset > maxoff) {
        win->row_offset = maxoff;
        if (win->row_offset < 0)
            win->row_offset = 0;
    }
}

void buf_scroll_half_page_up(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;
    if (buf->filename)
        jump_list_add(&E.jump_list, buf->filename, win->cursor.x,
                      win->cursor.y);
    int half_page = E.screen_rows / 2;
    win->cursor.y -= half_page;
    if (win->cursor.y < 0)
        win->cursor.y = 0;
}

void buf_scroll_half_page_down(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;
    if (buf->filename)
        jump_list_add(&E.jump_list, buf->filename, win->cursor.x,
                      win->cursor.y);
    int half_page = E.screen_rows / 2;
    win->cursor.y += half_page;
    if (win->cursor.y >= buf->num_rows) {
        win->cursor.y = buf->num_rows - 1;
        if (win->cursor.y < 0)
            win->cursor.y = 0;
    }
}

/*** Navigation ***/

void buf_goto_line(int line_num) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;

    /* Convert from 1-indexed to 0-indexed */
    line_num--;
    if (line_num < 0)
        line_num = 0;
    if (line_num >= buf->num_rows)
        line_num = buf->num_rows - 1;

    if (abs(line_num - win->cursor.y) >= 5 && buf->filename)
        jump_list_add(&E.jump_list, buf->filename, win->cursor.x,
                      win->cursor.y);

    win->cursor.y = line_num;
    win->cursor.x = 0;
}

void buf_find_matching_bracket(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win))
        return;
    cur_sync_from_window(buf, win);
    if (!BOUNDS_CHECK(buf->cursor->y, buf->num_rows)) {
        cur_sync_to_window(buf, win);
        return;
    }

    Row *row = &buf->rows[buf->cursor->y];
    if (buf->cursor->x >= (int)row->chars.len)
        return;

    char ch = row->chars.data[buf->cursor->x];
    char match;
    int direction;

    /* Determine bracket type and direction */
    if (ch == '(' || ch == '{' || ch == '[') {
        direction = 1; /* Forward */
        if (ch == '(')
            match = ')';
        else if (ch == '{')
            match = '}';
        else
            match = ']';
    } else if (ch == ')' || ch == '}' || ch == ']') {
        direction = -1; /* Backward */
        if (ch == ')')
            match = '(';
        else if (ch == '}')
            match = '{';
        else
            match = '[';
    } else {
        return; /* Not a bracket */
    }

    int depth = 1;
    int y = buf->cursor->y;
    int x = buf->cursor->x + direction;

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
                    buf->cursor->y = y;
                    buf->cursor->x = x;
                    cur_sync_to_window(buf, win);
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
    cur_sync_to_window(buf, win);
}

/*** Cursor movement ***/

/* Usable content columns of a soft-wrapped window: width minus the
 * gutter (fixed, or sized to the largest line number it must show).
 * Mirrors the renderer's gutter sizing. */
static int wrap_content_cols(const Buffer *buf, const Window *win) {
    int gutter = 0;
    if (win->gutter_mode == 2) {
        gutter = win->gutter_fixed_width;
        if (gutter < 0)
            gutter = 0;
    } else if (!(win->gutter_mode == 0 && !E.show_line_numbers)) {
        int maxline = buf->num_rows;
        if (E.relative_line_numbers) {
            int maxrel = win->height;
            if (maxrel < 1)
                maxrel = 1;
            maxline = maxrel;
        }
        gutter = 0;
        int tmp = maxline;
        while (tmp > 0) {
            gutter++;
            tmp /= 10;
        }
        if (gutter < 2)
            gutter = 2;
    }
    int margin = gutter ? (gutter + 1) : 0;
    int content_cols = win->width - margin;
    return content_cols > 0 ? content_cols : 1;
}

void buf_move_cursor_key(int key) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    Row *row = (win->cursor.y >= 0 && win->cursor.y < buf->num_rows)
                   ? &buf->rows[win->cursor.y]
                   : NULL;
    switch (key) {
    case KEY_ARROW_LEFT:
    case 'h':
        if (row) {
            if (win->cursor.x > 0) {
                /* Step back one whole codepoint (skip continuation bytes). */
                int idx = win->cursor.x - 1;
                while (idx > 0 &&
                       ((unsigned char)row->chars.data[idx] & 0xC0) == 0x80)
                    idx--;
                win->cursor.x = idx;
            } else if (win->cursor.y > 0) {
                win->cursor.y--;
                win->cursor.x = (int)buf->rows[win->cursor.y].chars.len;
            }
        }
        break;
    case KEY_ARROW_DOWN:
    case 'j':
        if (win->wrap) {
            int content_cols = wrap_content_cols(buf, win);
            int vis_col = 0;
            int cur_vis =
                cursor_visual_position(buf, win, content_cols, &vis_col);
            int total_vis = buffer_total_visual_rows(buf, content_cols);
            if (cur_vis < total_vis - 1) {
                int target = cur_vis + 1;
                cursor_from_visual(buf, win, target, content_cols, vis_col);
            }
        } else {
            /* Carry the render column, not the byte index: tabs and
             * multibyte chars make the two diverge between rows. */
            if (win->cursor.y < buf->num_rows - 1) {
                if (row) {
                    int rx = buf_row_cx_to_rx(row, win->cursor.x);
                    win->cursor.x =
                        buf_row_rx_to_cx(&buf->rows[win->cursor.y + 1], rx);
                }
                win->cursor.y++;
            }
        }
        break;
    case KEY_ARROW_UP:
    case 'k':
        if (win->wrap) {
            int content_cols = wrap_content_cols(buf, win);
            int vis_col = 0;
            int cur_vis =
                cursor_visual_position(buf, win, content_cols, &vis_col);
            if (cur_vis > 0) {
                int target = cur_vis - 1;
                cursor_from_visual(buf, win, target, content_cols, vis_col);
            }
        } else {
            /* See ARROW_DOWN: preserve the render column across rows. */
            if (win->cursor.y > 0) {
                if (row) {
                    int rx = buf_row_cx_to_rx(row, win->cursor.x);
                    win->cursor.x =
                        buf_row_rx_to_cx(&buf->rows[win->cursor.y - 1], rx);
                }
                win->cursor.y--;
            }
        }
        break;
    case KEY_ARROW_RIGHT:
    case 'l':
        if (row) {
            if (win->cursor.x < (int)row->chars.len) {
                /* Step forward one whole codepoint. */
                int adv = 1;
                utf8_char_width(row->chars.data + win->cursor.x,
                                row->chars.len - win->cursor.x, &adv);
                if (adv < 1)
                    adv = 1;
                win->cursor.x += adv;
            } else if (win->cursor.y < buf->num_rows - 1) {
                win->cursor.y++;
                win->cursor.x = 0;
            }
        }
        break;
    }
    row = (win->cursor.y >= 0 && win->cursor.y < buf->num_rows)
              ? &buf->rows[win->cursor.y]
              : NULL;
    int rowlen = row ? (int)row->chars.len : 0;
    if (win->cursor.x > rowlen)
        win->cursor.x = rowlen;
    if (win->cursor.x < 0)
        win->cursor.x = 0;
    /* Vertical moves carry the byte index across lines; make sure we never
     * land on a UTF-8 continuation byte. */
    if (row && win->cursor.x > 0 && win->cursor.x < rowlen) {
        while (win->cursor.x > 0 &&
               ((unsigned char)row->chars.data[win->cursor.x] & 0xC0) == 0x80)
            win->cursor.x--;
    }
}
