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

    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

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

char *buf_rows_to_string(int *buflen) {
    Buffer *buf = buf_cur();
    if (!buf) {
        *buflen = 0;
        return NULL;
    }

    size_t totlen = 0;
    for (int j = 0; j < buf->num_rows; j++)
        totlen += buf->rows[j].chars.len + 1;
    *buflen = totlen;

    char *buffer = malloc(totlen);
    char *p = buffer;
    for (int j = 0; j < buf->num_rows; j++) {
        memcpy(p, buf->rows[j].chars.data, buf->rows[j].chars.len);
        p += buf->rows[j].chars.len;
        *p = '\n';
        p++;
    }

    return buffer;
}

void buf_open(char *filename) {
    Buffer *buf = buf_cur();

    /* If current buffer is empty and unnamed, use it */
    if (buf && buf->num_rows == 0 && buf->filename == NULL) {
        /* Use current buffer */
    } else {
        /* Create new buffer */
        int idx = buf_new(filename);
        if (idx < 0) return;
        E.current_buffer = idx;
        Window *win = window_cur();
        if (win) win->buffer_index = idx;
        buf = buf_cur();
    }

    if (filename) {
        free(buf->filename);
        buf->filename = strdup(filename);
        free(buf->filetype);
        buf->filetype = buf_detect_filetype(filename);
    }

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        ed_set_status_message("New file: %s", filename);
        return;
    }

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 && (line[linelen - 1] == '\n' ||
                               line[linelen - 1] == '\r'))
            linelen--;
        buf_row_insert(buf->num_rows, line, linelen);
    }
    free(line);
    fclose(fp);
    buf->dirty = 0;

    /* Fire hook */
    HookBufferEvent event = {buf, filename};
    hook_fire_buffer(HOOK_BUFFER_OPEN, &event);

    ed_set_status_message("Loaded: %s", filename);
}

void buf_save(void) {
    Buffer *buf = buf_cur();
    if (!buf) return;

    if (buf->filename == NULL) {
        ed_set_status_message("No filename");
        return;
    }

    int len;
    char *buffer = buf_rows_to_string(&len);

    int fd = open(buf->filename, O_RDWR | O_CREAT, 0644);
    if (fd != -1) {
        if (ftruncate(fd, len) != -1) {
            if (write(fd, buffer, len) == len) {
                close(fd);
                free(buffer);
                buf->dirty = 0;

                /* Fire hook */
                HookBufferEvent event = {buf, buf->filename};
                hook_fire_buffer(HOOK_BUFFER_SAVE, &event);

                ed_set_status_message("%d bytes written to %s", len, buf->filename);
                return;
            }
        }
        close(fd);
    }

    free(buffer);
    ed_set_status_message("Error saving: %s", strerror(errno));
}

/*** Output ***/
static inline void ab_append_ch(Abuf *ab, char c) { ab_append(ab, &c, 1); }

void window_scroll(Window *win) {
    Buffer *buf = NULL;
    if (!win) return;
    if (E.num_buffers > 0 && win->buffer_index >= 0 && win->buffer_index < E.num_buffers)
        buf = &E.buffers[win->buffer_index];
    if (!buf || !win) return;

    E.render_x = 0;
    if (win->cursor_y < buf->num_rows) {
        E.render_x = buf_row_cx_to_rx(&buf->rows[win->cursor_y], win->cursor_x);
    }

    if (win->cursor_y < win->row_offset) {
        win->row_offset = win->cursor_y;
    }
    if (win->cursor_y >= win->row_offset + win->height) {
        win->row_offset = win->cursor_y - win->height + 1;
    }
    if (E.render_x < win->col_offset) {
        win->col_offset = E.render_x;
    }
    if (E.render_x >= win->col_offset + win->width) {
        win->col_offset = E.render_x - win->width + 1;
    }
}

static int ln_gutter_width(int view_rows) {
    if (!E.show_line_numbers) return 0;
    Buffer *buf = buf_cur();
    int maxline = 1;
    if (buf && buf->num_rows > 0) {
        if (E.relative_line_numbers) {
            /* Worst-case relative value in view is roughly screen height */
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

static void ed_draw_rows_win(Abuf *ab, const Window *win) {
    Buffer *buf = NULL;
    if (E.num_buffers > 0 && win->buffer_index >= 0 && win->buffer_index < E.num_buffers)
        buf = &E.buffers[win->buffer_index];
    log_msg("draw win top=%d left=%d h=%d w=%d buf=%p num_rows=%d y=%d x=%d off=%d,%d", win->top, win->left, win->height, win->width, (void*)buf, buf?buf->num_rows:-1, win->cursor_y, win->cursor_x, win->row_offset, win->col_offset);
    int gutter = ln_gutter_width(win->height);
    int margin = gutter ? (gutter + 1) : 0; /* number + space */
    int content_cols = win->width - margin;
    if (content_cols < 0) content_cols = 0;

    for (int y = 0; y < win->height; y++) {
        int filerow = y + (buf ? win->row_offset : 0);

        /* Move to start of this window row */
        ansi_move(ab, win->top + y, win->left);

        /* Draw gutter */
        if (E.show_line_numbers) {
            if (buf && filerow < buf->num_rows) {
                char nb[32];
                int num = filerow + 1;
                if (E.relative_line_numbers && buf) {
                    int cur = win->cursor_y;
                    if (filerow != cur) num = abs(filerow - cur);
                }
                int n = snprintf(nb, sizeof(nb), "%*d ", gutter, num);
                ab_append(ab, nb, n);
            } else {
                for (int i = 0; i < margin; i++) ab_append_ch(ab, ' ');
            }
        }

        if (!buf || filerow >= buf->num_rows) {
            if ((!buf || buf->num_rows == 0) && y == win->height / 3) {
            } else {
                ab_append_ch(ab, '~');
            }
        } else {
            int line_rlen = (int)buf->rows[filerow].render.len;
            int len = line_rlen - win->col_offset;
            if (len < 0) len = 0;
            if (len > content_cols) len = content_cols;
            if (len > 0) {
                int sel_active = (E.mode == MODE_VISUAL && win->focus);
                int has_sel = 0;
                int sel_start_rx = 0, sel_end_rx = 0;
                if (sel_active) {
                    int ay = win->visual_start_y, ax = win->visual_start_x;
                    int by = win->cursor_y, bx = win->cursor_x;
                    int sy = ay, sx = ax, ey = by, ex = bx;
                    if (sy > ey || (sy == ey && sx > ex)) {
                        sy = by; sx = bx; ey = ay; ex = ax;
                    }
                    if (filerow >= sy && filerow <= ey) {
                        has_sel = 1;
                        if (sy == ey) {
                            int rs = buf_row_cx_to_rx(&buf->rows[filerow], sx);
                            int re = buf_row_cx_to_rx(&buf->rows[filerow], ex);
                            sel_start_rx = rs; sel_end_rx = re;
                        } else if (filerow == sy) {
                            int rs = buf_row_cx_to_rx(&buf->rows[filerow], sx);
                            sel_start_rx = rs; sel_end_rx = line_rlen;
                        } else if (filerow == ey) {
                            int re = buf_row_cx_to_rx(&buf->rows[filerow], ex);
                            sel_start_rx = 0; sel_end_rx = re;
                        } else {
                            sel_start_rx = 0; sel_end_rx = line_rlen;
                        }
                        if (sel_start_rx < 0) sel_start_rx = 0;
                        if (sel_end_rx > line_rlen) sel_end_rx = line_rlen;
                        if (sel_end_rx < sel_start_rx) has_sel = 0;
                    }
                }

                #define APPEND_SLICE(start_rx_, slice_cols_) do { \
                    int __sr = (start_rx_); int __len = (slice_cols_); \
                    if (__len > 0) { \
                        if (ts_is_enabled()) { \
                            char linebuf[4096]; \
                            size_t wrote = ts_highlight_line(buf, filerow, linebuf, sizeof(linebuf), __sr, __len); \
                            if (wrote > 0) { \
                                if ((int)wrote > __len) wrote = (size_t)__len; \
                                ab_append(ab, linebuf, (int)wrote); \
                            } else { \
                                if (__sr + __len > line_rlen) __len = line_rlen - __sr; \
                                if (__len > 0) ab_append(ab, &buf->rows[filerow].render.data[__sr], __len); \
                            } \
                        } else { \
                            if (__sr + __len > line_rlen) __len = line_rlen - __sr; \
                            if (__len > 0) ab_append(ab, &buf->rows[filerow].render.data[__sr], __len); \
                        } \
                    } \
                } while(0)

                if (!has_sel) {
                    APPEND_SLICE(win->col_offset, len);
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
    }
}

void buf_refresh_screen(void) {
    /* Live resize: get current terminal size and compute base content rows. */
    int term_rows = E.screen_rows + 2; /* fallback if call fails */
    int term_cols = E.screen_cols;
    (void)get_window_size(&term_rows, &term_cols);
    E.screen_cols = term_cols;
    int base_rows = term_rows > 2 ? (term_rows - 2) : term_rows;
    log_msg("frame: term=%dx%d base_rows=%d qf=%d", term_rows, term_cols, base_rows, (E.qf.open?E.qf.height:0));

    /* Compute layout and apply content rows */
    Layout lo; layout_compute(&lo);
    E.screen_cols = lo.term_cols;
    E.screen_rows = lo.content_rows;
    windows_on_resize(lo.content_rows, lo.term_cols);
    log_msg("windows: count=%d layout=%d avail_rows=%d", E.num_windows, E.window_layout, lo.content_rows);

    /* Scroll active window */
    Window *win = window_cur();
    window_scroll(win);

    Abuf ab; ab_init(&ab);
    ansi_hide_cursor(&ab);
    ansi_home(&ab);

    /* Draw all windows */
    for (int wi = 0; wi < E.num_windows; ++wi) {
        ed_draw_rows_win(&ab, &E.windows[wi]);
    }
    draw_status_bar(&ab, &lo);
    if (E.qf.open && E.qf.height > 0) draw_quickfix(&ab, &lo);
    draw_message_bar(&ab, &lo);

    Buffer *buf = NULL;
    if (E.num_buffers > 0 && win->buffer_index >= 0 && win->buffer_index < E.num_buffers)
        buf = &E.buffers[win->buffer_index];
    int gutter = ln_gutter_width(win->height);
    int margin = gutter ? (gutter + 1) : 0;
    int cur_row;
    int cur_col;
    if (E.mode == MODE_COMMAND) {
        int base_row = E.screen_rows + 2 + (E.qf.open && E.qf.height > 0 ? E.qf.height : 0);
        cur_row = base_row;
        cur_col = 2 + E.command_len; /* ':' + content */
    } else if (E.qf.open && E.qf.focus) {
        cur_row = E.screen_rows + 2; /* status bar is one line; quickfix header is next */
        cur_col = 1;
    } else {
        cur_row = (buf ? win->cursor_y - win->row_offset : 0) + win->top;
        cur_col = (E.render_x - (buf ? win->col_offset : 0)) + win->left + margin;
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
