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

void window_scroll(Window *win) {
    Buffer *buf = NULL;
    if (!win) return;
    if (E.buffers.len > 0 && win->buffer_index >= 0 && win->buffer_index < (int)E.buffers.len)
        buf = &E.buffers.data[win->buffer_index];
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

/* UTF-8 helpers for render slicing: treat each codepoint as width=1 (tabs already expanded) */
static int render_cols_ss(const SizedStr *r) {
    int cols = 0; const unsigned char *p = (const unsigned char*)r->data; int n = (int)r->len;
    for (int i = 0; i < n;) {
        unsigned char c = p[i];
        int adv = (c & 0x80) == 0 ? 1 : ((c & 0xE0) == 0xC0 ? 2 : ((c & 0xF0) == 0xE0 ? 3 : ((c & 0xF8) == 0xF0 ? 4 : 1)));
        if (i + adv > n) adv = 1; i += adv; cols += 1;
    }
    return cols;
}
static void render_slice_ss(const SizedStr *r, int start_col, int want_cols, int *out_start, int *out_len) {
    const unsigned char *p = (const unsigned char*)r->data; int n = (int)r->len;
    int col = 0; int i = 0;
    while (i < n && col < start_col) {
        unsigned char c = p[i]; int adv = (c & 0x80) == 0 ? 1 : ((c & 0xE0) == 0xC0 ? 2 : ((c & 0xF0) == 0xE0 ? 3 : ((c & 0xF8) == 0xF0 ? 4 : 1)));
        if (i + adv > n) adv = 1; i += adv; col++;
    }
    int sb = i; int taken = 0;
    while (i < n && taken < want_cols) {
        unsigned char c = p[i]; int adv = (c & 0x80) == 0 ? 1 : ((c & 0xE0) == 0xC0 ? 2 : ((c & 0xF0) == 0xE0 ? 3 : ((c & 0xF8) == 0xF0 ? 4 : 1)));
        if (i + adv > n) adv = 1; i += adv; taken++;
    }
    if (out_start) *out_start = sb; if (out_len) *out_len = i - sb;
}

static void draw_quickfix_window(Abuf *ab, const Window *win) {
    int width = win->width;
    /* Header */
    char hdr[128];
    int hlen = snprintf(hdr, sizeof(hdr), " Quickfix (%d items)  j/k navigate  Enter open  q close ", E.qf.len);
    if (hlen > width) hlen = width;
    ansi_move(ab, win->top, win->left);
    if (hlen > 0) ab_append(ab, hdr, hlen);
    ansi_sgr_reset(ab);
    ansi_clear_eol(ab);
    /* Items */
    int lines = win->height - 1; if (lines < 0) lines = 0;
    int start = E.qf.scroll;
    for (int row = 0; row < lines; row++) {
        int idx = start + row;
        ansi_move(ab, win->top + 1 + row, win->left);
        if (idx >= E.qf.len) { ansi_clear_eol(ab); continue; }
        const QfItem *it = &E.qf.items[idx];
        char line[512]; int l = 0;
        if (it->filename && it->filename[0]) l = snprintf(line, sizeof(line), "%s:%d:%d: %s", it->filename, it->line, it->col, it->text ? it->text : "");
        else l = snprintf(line, sizeof(line), "%d:%d: %s", it->line, it->col, it->text ? it->text : "");
        if (l > width) l = width;
        if (idx == E.qf.sel) ansi_invert_on(ab);
        if (l > 0) ab_append(ab, line, l);
        ansi_sgr_reset(ab);
        ansi_clear_eol(ab);
    }
}

static void ed_draw_rows_win(Abuf *ab, const Window *win) {
    if (win->is_quickfix) { draw_quickfix_window(ab, win); return; }
    if (win->is_term)     { term_pane_draw(win, ab);      return; }
    Buffer *buf = NULL;
    if (E.buffers.len > 0 && win->buffer_index >= 0 && win->buffer_index < (int)E.buffers.len)
        buf = &E.buffers.data[win->buffer_index];
    int gutter = window_gutter_width(win, win->height);
    int margin = gutter ? (gutter + 1) : 0; /* number + space */
    int content_cols = win->width - margin;
    if (content_cols < 0) content_cols = 0;

    /* helper functions moved to file scope */

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
            int line_rcols = render_cols_ss(&buf->rows[filerow].render);
            int len = line_rcols - win->col_offset;
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
                            sel_start_rx = rs; sel_end_rx = line_rcols;
                        } else if (filerow == ey) {
                            int re = buf_row_cx_to_rx(&buf->rows[filerow], ex);
                            sel_start_rx = 0; sel_end_rx = re;
                        } else {
                            sel_start_rx = 0; sel_end_rx = line_rcols;
                        }
                        if (sel_start_rx < 0) sel_start_rx = 0;
                        if (sel_end_rx > line_rcols) sel_end_rx = line_rcols;
                        if (sel_end_rx < sel_start_rx) has_sel = 0;
                    }
                }

                #define APPEND_SLICE(start_rx_, slice_cols_) do { \
                    int __sb = 0, __blen = 0; \
                    render_slice_ss(&buf->rows[filerow].render, (start_rx_), (slice_cols_), &__sb, &__blen); \
                    if (__blen > 0) { \
                        if (ts_is_enabled()) { \
                            char linebuf[4096]; \
                            size_t wrote = ts_highlight_line(buf, filerow, linebuf, sizeof(linebuf), __sb, __blen); \
                            if (wrote > 0) { \
                                if ((int)wrote > __blen) wrote = (size_t)__blen; \
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
    /* Keep quickfix as a bottom quickfix window within the root layout */
    wlayout_sync_quickfix(&E.wlayout_root, E.qf.open && E.qf.height > 0, E.qf.height);
    /* Keep terminal pane as a bottom window when open */
    if (E.term_open && E.term_window_index >= 0 && E.term_window_index < (int)E.windows.len) {
        wlayout_sync_term(&E.wlayout_root, 1, E.term_height, E.term_window_index);
    }
    /* If qf requested focus, switch to its window once created */
    if (E.qf.open && E.qf.focus) {
        for (int i = 0; i < (int)E.windows.len; i++) if (E.windows.data[i].is_quickfix) {
            for (int j = 0; j < (int)E.windows.len; j++) E.windows.data[j].focus = 0;
            E.current_window = i; E.windows.data[i].focus = 1; E.current_buffer = E.windows.data[i].buffer_index;
            E.qf.focus = 0; /* one-time handover */
            break;
        }
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
    } else if (win && win->is_quickfix) {
        cur_row = win->top + 1 + (E.qf.sel - E.qf.scroll);
        if (cur_row < win->top + 1) cur_row = win->top + 1;
        if (cur_row > win->top + win->height - 1) cur_row = win->top + win->height - 1;
        cur_col = 1;
    } else if (win && win->is_term) {
        cur_row = win->top;
        cur_col = win->left;
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
