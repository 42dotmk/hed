#include "hed.h"
#include <stdarg.h>
#include "abuf.h"
#include "ansi.h"
#include "bottom_ui.h"

struct termios orig_termios;

/*** Terminal ***/

void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}

void disable_raw_mode(void) {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
        die("tcsetattr");
}

void enable_raw_mode(void) {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");
    atexit(disable_raw_mode);

    struct termios raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int get_cursor_position(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') 
        return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) 
        return -1;

    return 0;
}

int get_window_size(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return get_cursor_position(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** File I/O ***/

static char *buf_rows_to_string_in(Buffer *buf, int *out_len) {
    if (out_len) *out_len = 0;
    if (!buf) return NULL;
    size_t totlen = 0;
    for (int j = 0; j < buf->num_rows; j++) 
        totlen += buf->rows[j].chars.len + 1;

    if (out_len) *out_len = (int)totlen;
    char *buffer = malloc(totlen);
    char *p = buffer;
    for (int j = 0; j < buf->num_rows; j++) {
        memcpy(p, buf->rows[j].chars.data, buf->rows[j].chars.len);
        p += buf->rows[j].chars.len;
        *p++ = '\n';
    }
    return buffer;
}

EdError buf_save_in(Buffer *buf) {
    if (!PTR_VALID(buf)) return ED_ERR_INVALID_ARG;

    if (buf->filename == NULL) {
        ed_set_status_message("No filename");
        return ED_ERR_FILE_NOT_FOUND;
    }

    int len = 0;
    char *buffer = buf_rows_to_string_in(buf, &len);
    if (!buffer) return ED_ERR_NOMEM;

    int fd = open(buf->filename, O_RDWR | O_CREAT, 0644);
    if (fd == -1) {
        free(buffer);
        ed_set_status_message("Error opening file: %s", strerror(errno));
        return ED_ERR_FILE_OPEN;
    }

    if (ftruncate(fd, len) == -1) {
        close(fd);
        free(buffer);
        ed_set_status_message("Error truncating file: %s", strerror(errno));
        return ED_ERR_FILE_WRITE;
    }

    if (write(fd, buffer, len) != len) {
        close(fd);
        free(buffer);
        ed_set_status_message("Error writing file: %s", strerror(errno));
        return ED_ERR_FILE_WRITE;
    }

    close(fd);
    free(buffer);
    buf->dirty = 0;

    /* Track in recent files */
    recent_files_add(&E.recent_files, buf->filename);

    /* Fire hook */
    HookBufferEvent event = {buf, buf->filename};
    hook_fire_buffer(HOOK_BUFFER_SAVE, &event);

    ed_set_status_message("%d bytes written to %s", len, buf->filename);
    return ED_OK;
}

/*** Output ***/
static inline void ab_append_ch(Abuf *ab, char c) { ab_append(ab, &c, 1); }

static int window_gutter_width(const Window *win, int view_rows);
static int render_cols_ss(const SizedStr *r);
static int row_visual_height(const Row *row, int content_cols, int wrap) {
    if (!wrap) return 1;
    if (!row) return 1;
    if (content_cols <= 0) return 1;
    int rcols = render_cols_ss(&row->render);
    if (rcols <= 0) return 1;
    int h = (rcols + content_cols - 1) / content_cols;
    return h < 1 ? 1 : h;
}

void window_scroll(Window *win) {
    Buffer *buf = NULL;
    if (!win) return;
    if (E.buffers.len > 0 && win->buffer_index >= 0 && win->buffer_index < (int)E.buffers.len)
        buf = &E.buffers.data[win->buffer_index];
    if (!buf || !win) return;

    int gutter = window_gutter_width(win, win->height);
    int margin = gutter ? (gutter + 1) : 0;
    int content_cols = win->width - margin;
    if (content_cols <= 0) content_cols = 1;

    E.render_x = 0;
    if (win->cursor.y < buf->num_rows) {
        E.render_x = buf_row_cx_to_rx(&buf->rows[win->cursor.y], win->cursor.x);
    }

    if (!win->wrap) {
        if (win->cursor.y < win->row_offset) {
            win->row_offset = win->cursor.y;
        }
        if (win->cursor.y >= win->row_offset + win->height) {
            win->row_offset = win->cursor.y - win->height + 1;
        }
        if (E.render_x < win->col_offset) {
            win->col_offset = E.render_x;
        }
        if (E.render_x >= win->col_offset + win->width) {
            win->col_offset = E.render_x - win->width + 1;
        }
        return;
    }

    /* Wrap enabled: treat row_offset as visual (wrapped) row index */
    win->col_offset = 0; /* no horizontal scroll when wrapped */

    /* Compute cursor's visual row index */
    int cursor_visual = 0;
    for (int y = 0; y < buf->num_rows; y++) {
        Row *row = &buf->rows[y];
        int h = row_visual_height(row, content_cols, 1);
        if (y < win->cursor.y) {
            cursor_visual += h;
        } else if (y == win->cursor.y) {
            int rx = buf_row_cx_to_rx(row, win->cursor.x);
            if (rx < 0) rx = 0;
            int sub = rx / content_cols;
            if (sub >= h) sub = h - 1;
            cursor_visual += sub;
            break;
        } else {
            break;
        }
    }

    /* Total visual height */
    int total_visual = 0;
    for (int y = 0; y < buf->num_rows; y++) {
        total_visual += row_visual_height(&buf->rows[y], content_cols, 1);
    }

    int max_off = total_visual - win->height;
    if (max_off < 0) max_off = 0;

    if (cursor_visual < win->row_offset) {
        win->row_offset = cursor_visual;
    } else if (cursor_visual >= win->row_offset + win->height) {
        win->row_offset = cursor_visual - win->height + 1;
    }
    if (win->row_offset < 0) win->row_offset = 0;
    if (win->row_offset > max_off) win->row_offset = max_off;
}

static int window_gutter_width(const Window *win, int view_rows) {
    if (!win) return 0;
    if (win->gutter_mode == 2) {
        int w = win->gutter_fixed_width; if (w < 0) w = 0; return w;
    }
    if (win->gutter_mode == 0 && !E.show_line_numbers) return 0;
    /* Auto line-number mode: use current window's buffer for width calc */
    Buffer *buf = NULL;
    if (E.buffers.len > 0 && win->buffer_index >= 0 && win->buffer_index < (int)E.buffers.len)
        buf = &E.buffers.data[win->buffer_index];
    int maxline = 1;
    if (buf && buf->num_rows > 0) {
        if (E.relative_line_numbers) {
            int maxrel = view_rows;
            if (maxrel < 1) maxrel = 1;
            maxline = maxrel;
        } else {
            maxline = buf->num_rows;
        }
    }
    int w = 0; while (maxline > 0) { w++; maxline /= 10; }
    if (w < 2) w = 2;
    return w;
}

/* UTF-8 helpers for render slicing: use wcwidth() for proper wide char support */
static int render_cols_ss(const SizedStr *r) {
    return utf8_display_width(r->data, r->len);
}
static void render_slice_ss(const SizedStr *r, int start_col, int want_cols, int *out_start, int *out_len) {
    utf8_slice_by_columns(r->data, r->len, start_col, want_cols, out_start, out_len);
}

static void ed_draw_rows_win(Abuf *ab, const Window *win) {
    Buffer *buf = NULL;
    if (E.buffers.len > 0 && win->buffer_index >= 0 && win->buffer_index < (int)E.buffers.len)
        buf = &E.buffers.data[win->buffer_index];
    int gutter = window_gutter_width(win, win->height);
    int margin = gutter ? (gutter + 1) : 0; /* number + space */
    int content_cols = win->width - margin;
    if (content_cols < 0) content_cols = 0;

    /* helper functions moved to file scope */

    /* Determine starting logical row and wrapped sub-row based on visual offset */
    int row = 0;
    int sub = 0;
    if (buf) {
        int target = win->row_offset;
        int y = 0;
        while (y < buf->num_rows) {
            int h = row_visual_height(&buf->rows[y], content_cols, win->wrap);
            if (target < h) {
                row = y;
                sub = target;
                break;
            }
            target -= h;
            y++;
        }
        if (y >= buf->num_rows) {
            row = buf->num_rows;
            sub = 0;
        }
    }

    for (int vy = 0; vy < win->height; vy++) {
        int filerow = row;

        /* Move to start of this window row */
        ansi_move(ab, win->top + vy, win->left);

        /* Draw gutter */
        if (E.show_line_numbers) {
            if (buf && filerow < buf->num_rows) {
                char nb[32];
                int num = filerow + 1;
                if (E.relative_line_numbers && buf) {
                    int cur = win->cursor.y;
                    if (filerow != cur) num = abs(filerow - cur);
                }
                int n = snprintf(nb, sizeof(nb), "%*d ", gutter, num);
                ab_append(ab, nb, n);
            } else {
                for (int i = 0; i < margin; i++) ab_append_ch(ab, ' ');
            }
        }

        if (!buf || filerow >= buf->num_rows) {
            if ((!buf || buf->num_rows == 0) && vy == win->height / 3) {
            } else {
                ab_append_ch(ab, '~');
            }
        } else {
            int line_rcols = render_cols_ss(&buf->rows[filerow].render);
            int start_rx;
            int len;

            if (win->wrap) {
                start_rx = sub * content_cols;
                if (start_rx < 0) start_rx = 0;
                if (start_rx > line_rcols) start_rx = line_rcols;
                len = line_rcols - start_rx;
                if (len > content_cols) len = content_cols;
            } else {
                start_rx = win->col_offset;
                len = line_rcols - win->col_offset;
                if (len < 0) len = 0;
                if (len > content_cols) len = content_cols;
            }

            if (len > 0) {
                int has_sel = 0;
                int sel_start_rx = 0, sel_end_rx = 0;

                #define APPEND_SLICE(start_rx_, slice_cols_) do { \
                    int __sb = 0, __blen = 0; \
                    render_slice_ss(&buf->rows[filerow].render, (start_rx_), (slice_cols_), &__sb, &__blen); \
                    if (__blen > 0) { \
                        if (ts_is_enabled()) { \
                            char linebuf[4096]; \
                            size_t wrote = ts_highlight_line(buf, filerow, linebuf, sizeof(linebuf), __sb, __blen); \
                            if (wrote > 0) { \
                                ab_append(ab, linebuf, (int)wrote); \
                            } else { \
                                ab_append(ab, &buf->rows[filerow].render.data[__sb], __blen); \
                            } \
                        } else { \
                            ab_append(ab, &buf->rows[filerow].render.data[__sb], __blen); \
                        } \
                    } \
                } while(0)

                if (!has_sel) {
                    APPEND_SLICE(start_rx, len);
                } else {
                    int vis_start = win->col_offset;
                    int vis_end = win->col_offset + content_cols;

                    int pre_start = vis_start;
                    int pre_end = sel_start_rx < vis_end ? (sel_start_rx > vis_start ? sel_start_rx : vis_start) : vis_end;
                    int pre_len = pre_end - pre_start;
                    APPEND_SLICE(pre_start, pre_len);

                    int sel_vis_start = sel_start_rx < vis_start ? vis_start : sel_start_rx;
                    int sel_vis_end = sel_end_rx > vis_end ? vis_end : sel_end_rx;
                    int sel_len = sel_vis_end - sel_vis_start;
                    if (sel_len > 0) {
                        ansi_invert_on(ab); /* inverse */
                        APPEND_SLICE(sel_vis_start, sel_len);
                        ansi_sgr_reset(ab);  /* reset */
                    }

                    int post_start = sel_end_rx > vis_start ? (sel_end_rx < vis_end ? sel_end_rx : vis_end) : vis_start;
                    int post_len = vis_end - post_start;
                    APPEND_SLICE(post_start, post_len);
                }
                #undef APPEND_SLICE
            }
        }

        ansi_clear_eol(ab);

        /* Advance to next visual row */
        if (buf && row < buf->num_rows) {
            if (win->wrap) {
                Row *r = &buf->rows[row];
                int h = row_visual_height(r, content_cols, 1);
                sub++;
                if (sub >= h) {
                    sub = 0;
                    row++;
                }
            } else {
                row++;
            }
        }
    }
}

void ed_render_frame(void) {
    /* Live resize: get current terminal size and compute base content rows. */
    int term_rows = E.screen_rows + 2; /* fallback if call fails */
    int term_cols = E.screen_cols;
    (void)get_window_size(&term_rows, &term_cols);
    E.screen_cols = term_cols;
    /* Compute layout and apply content rows */
    Layout lo; layout_compute(&lo);
    E.screen_cols = lo.term_cols;
    E.screen_rows = lo.content_rows;
    if (!E.wlayout_root) {
        /* Always keep a root layout present */
        E.wlayout_root = wlayout_init_root(0);
    }
    wlayout_compute(E.wlayout_root, 1, 1, lo.content_rows, lo.term_cols);

    /* Scroll active window */
    Window *win = window_cur();
    window_scroll(win);

    Abuf ab; ab_init(&ab);
    ansi_hide_cursor(&ab);
    ansi_home(&ab);

    /* Ensure highlighting is parsed for buffers used in windows */
    if (ts_is_enabled()) {
        for (int wi = 0; wi < (int)E.windows.len; ++wi) {
            int bi = E.windows.data[wi].buffer_index;
            if (bi >= 0 && bi < (int)E.buffers.len) {
                ts_buffer_autoload(&E.buffers.data[bi]);
                ts_buffer_reparse(&E.buffers.data[bi]);
            }
        }
    }

    /* Draw all windows */
    for (int wi = 0; wi < (int)E.windows.len; ++wi) {
        ed_draw_rows_win(&ab, &E.windows.data[wi]);
    }
    /* Draw decorations (borders/splits) over content */
    if (E.wlayout_root) {
        wlayout_draw_decorations(&ab, E.wlayout_root);
    }
    draw_status_bar(&ab, &lo);
    draw_message_bar(&ab, &lo);

    Buffer *buf = NULL;
    if (E.buffers.len > 0 && win->buffer_index >= 0 && win->buffer_index < (int)E.buffers.len)
        buf = &E.buffers.data[win->buffer_index];
    int gutter = window_gutter_width(win, win->height);
    int margin = gutter ? (gutter + 1) : 0;
    int cur_row;
    int cur_col;
    if (E.mode == MODE_COMMAND) {
        cur_row = lo.cmd_row;
        cur_col = 2 + E.command_len; /* ':' + content */
    } else {
        if (!buf || win->cursor.y >= buf->num_rows) {
            cur_row = win->top;
            cur_col = win->left + margin;
        } else if (!win->wrap) {
            cur_row = (win->cursor.y - win->row_offset) + win->top;
            cur_col = (E.render_x - win->col_offset) + win->left + margin;
        } else {
            /* Map cursor to visual row/column when wrapped */
            int content_cols = win->width - margin;
            if (content_cols <= 0) content_cols = 1;

            int visual_row = 0;
            for (int y = 0; y < buf->num_rows; y++) {
                Row *row = &buf->rows[y];
                int h = row_visual_height(row, content_cols, 1);
                if (y < win->cursor.y) {
                    visual_row += h;
                } else if (y == win->cursor.y) {
                    int rx = buf_row_cx_to_rx(row, win->cursor.x);
                    if (rx < 0) rx = 0;
                    int sub = rx / content_cols;
                    if (sub >= h) sub = h - 1;
                    visual_row += sub;
                    break;
                } else {
                    break;
                }
            }
            cur_row = (visual_row - win->row_offset) + win->top;

            /* Horizontal: position within current wrapped segment */
            int rx = E.render_x;
            if (rx < 0) rx = 0;
            int vis_col = rx % content_cols;
            cur_col = vis_col + win->left + margin;
        }
    }
    ansi_move(&ab, cur_row, cur_col);
    ansi_show_cursor(&ab);

    write(STDOUT_FILENO, ab.data, (size_t)ab.len);
    ab_free(&ab);
}

void ed_set_status_message(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.status_msg, sizeof(E.status_msg), fmt, ap);
    va_end(ap);
    log_msg("status: %s", E.status_msg);
}
