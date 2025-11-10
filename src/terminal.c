#include "hed.h"
#include <stdarg.h>

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

void buf_scroll(void) {
    Buffer *buf = buf_cur();
    if (!buf) return;

    E.render_x = 0;
    if (buf->cursor_y < buf->num_rows) {
        E.render_x = buf_row_cx_to_rx(&buf->rows[buf->cursor_y], buf->cursor_x);
    }

    if (buf->cursor_y < buf->row_offset) {
        buf->row_offset = buf->cursor_y;
    }
    if (buf->cursor_y >= buf->row_offset + E.screen_rows) {
        buf->row_offset = buf->cursor_y - E.screen_rows + 1;
    }
    if (E.render_x < buf->col_offset) {
        buf->col_offset = E.render_x;
    }
    if (E.render_x >= buf->col_offset + E.screen_cols) {
        buf->col_offset = E.render_x - E.screen_cols + 1;
    }
}

static int ln_gutter_width(void) {
    if (!E.show_line_numbers) return 0;
    Buffer *buf = buf_cur();
    int maxline = 1;
    if (buf && buf->num_rows > 0) {
        if (E.relative_line_numbers) {
            /* Worst-case relative value in view is roughly screen height */
            int maxrel = E.screen_rows;
            if (maxrel < 1) maxrel = 1;
            maxline = maxrel;
        } else {
            maxline = buf->num_rows;
        }
    }
    int w = 0; while (maxline > 0) { w++; maxline /= 10; }
    if (w < 2) w = 2; /* minimum width */
    return w;
}

void ed_draw_rows(char *ab, int *ablen, int maxlen) {
    Buffer *buf = buf_cur();
    int gutter = ln_gutter_width();
    int margin = gutter ? (gutter + 1) : 0; /* number + space */
    int content_cols = E.screen_cols - margin;
    if (content_cols < 0) content_cols = 0;

    for (int y = 0; y < E.screen_rows; y++) {
        int filerow = y + (buf ? buf->row_offset : 0);

        /* Draw gutter */
        if (E.show_line_numbers) {
            if (buf && filerow < buf->num_rows) {
                char nb[32];
                int num = filerow + 1;
                if (E.relative_line_numbers && buf) {
                    int cur = buf->cursor_y;
                    if (filerow != cur) num = abs(filerow - cur);
                }
                int n = snprintf(nb, sizeof(nb), "%*d ", gutter, num);
                memcpy(&ab[*ablen], nb, n); *ablen += n;
            } else {
                for (int i = 0; i < margin; i++) ab[(*ablen)++] = ' ';
            }
        }

        if (!buf || filerow >= buf->num_rows) {
            if ((!buf || buf->num_rows == 0) && y == E.screen_rows / 3) {
            } else {
                ab[(*ablen)++] = '~';
            }
        } else {
            int len = buf->rows[filerow].render.len - buf->col_offset;
            if (len < 0) len = 0;
            if (len > content_cols) len = content_cols;
            if (len > 0) {
                memcpy(&ab[*ablen], &buf->rows[filerow].render.data[buf->col_offset], len);
                *ablen += len;
            }
        }

        memcpy(&ab[*ablen], "\x1b[K", 3);
        *ablen += 3;
        ab[(*ablen)++] = '\r';
        ab[(*ablen)++] = '\n';
    }
    (void)maxlen;
}

void ed_draw_status_bar(char *ab, int *ablen, int maxlen) {
    (void)maxlen;
    Buffer *buf = buf_cur();

    // memcpy(&ab[*ablen], "\x1b[7m", 4);
    // *ablen += 4;

    char status[80], rstatus[80];
    char *mode_str = "";
    switch (E.mode) {
        case MODE_NORMAL: mode_str = "NORMAL"; break;
        case MODE_INSERT: mode_str = "INSERT"; break;
        case MODE_VISUAL: mode_str = "VISUAL"; break;
        case MODE_COMMAND: mode_str = "COMMAND"; break;
    }

    int len = snprintf(status, sizeof(status), " [%d/%d] %.20s - %d lines %s%s",
        E.current_buffer + 1, E.num_buffers,
        buf && buf->filename ? buf->filename : "[No Name]",
        buf ? buf->num_rows : 0,
        buf && buf->dirty ? "*" : "", mode_str);
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d:%d ",
        buf ? buf->cursor_y + 1 : 1, buf ? buf->cursor_x + 1 : 1);

    if (len > E.screen_cols) len = E.screen_cols;
    memcpy(&ab[*ablen], status, len);
    *ablen += len;

    while (len < E.screen_cols) {
        if (E.screen_cols - len == rlen) {
            memcpy(&ab[*ablen], rstatus, rlen);
            *ablen += rlen;
            break;
        } else {
            ab[(*ablen)++] = ' ';
            len++;
        }
    }
    // memcpy(&ab[*ablen], "\x1b[m", 3);
    // *ablen += 3;
    ab[(*ablen)++] = '\r';
    ab[(*ablen)++] = '\n';
}

/* Compute how many message lines are needed for the current status message. */
static int ed_message_lines_needed(void) {
    /* In command mode we always show a single input line. */
    if (E.mode == MODE_COMMAND) return 1;

    const char *s = E.status_msg;
    int cols = E.screen_cols > 0 ? E.screen_cols : 80;
    int lines = 1;
    int cur = 0;
    for (const char *p = s; *p; ++p) {
        if (*p == '\n') {
            lines++;
            cur = 0;
        } else {
            cur++;
            if (cur >= cols) { lines++; cur = 0; }
        }
    }
    if (lines < 1) lines = 1;
    return lines;
}

/* Current frame's chosen message lines (computed in buf_refresh_screen). */
static int g_msg_lines_current = 1;

void ed_draw_message_bar(char *ab, int *ablen, int maxlen) {
    (void)maxlen;
    if (E.mode == MODE_COMMAND) {
        memcpy(&ab[*ablen], "\x1b[K", 3);
        *ablen += 3;
        ab[(*ablen)++] = ':';
        int msglen = E.command_len;
        if (msglen > E.screen_cols - 1) msglen = E.screen_cols - 1;
        if (msglen > 0) {
            memcpy(&ab[*ablen], E.command_buf, msglen);
            *ablen += msglen;
        }
        return;
    }

    /* Draw multi-line status message (wrap at screen width, respect newlines). */
    const char *s = E.status_msg;
    int cols = E.screen_cols > 0 ? E.screen_cols : 80;
    int lines = g_msg_lines_current;
    int drawn = 0;
    const char *p = s;
    while (drawn < lines) {
        /* Clear this message line */
        memcpy(&ab[*ablen], "\x1b[K", 3);
        *ablen += 3;

        int used = 0;
        while (*p && *p != '\n' && used < cols) {
            ab[(*ablen)++] = *p++;
            used++;
        }
        /* If newline present, consume it and start next visual line. */
        if (*p == '\n') p++;

        /* Move to next terminal line if more to draw */
        drawn++;
        if (drawn < lines) {
            ab[(*ablen)++] = '\r';
            ab[(*ablen)++] = '\n';
        }
    }
}

static void ed_draw_quickfix(char *ab, int *ablen, int maxlen) {
    (void)maxlen;
    if (!E.qf.open || E.qf.height <= 0) return;
    int width = E.screen_cols;
    int visible = E.qf.height;
    if (visible < 1) return;

    /* Header */
    // memcpy(&ab[*ablen], "\x1b[7m", 4); *ablen += 4;
    char hdr[128];
    int hlen = snprintf(hdr, sizeof(hdr), " Quickfix (%d items)  j/k navigate  Enter open  q close ", E.qf.len);
    if (hlen > width) hlen = width;
    memcpy(&ab[*ablen], hdr, hlen); *ablen += hlen;
    memcpy(&ab[*ablen], "\x1b[m", 3); *ablen += 3;
    memcpy(&ab[*ablen], "\x1b[K\r\n", 5); *ablen += 5;

    /* Items */
    int lines = visible - 1;
    int start = E.qf.scroll;
    for (int row = 0; row < lines; row++) {
        int idx = start + row;
        if (idx >= E.qf.len) {
            memcpy(&ab[*ablen], "\x1b[K\r\n", 5); *ablen += 5;
            continue;
        }
        const QfItem *it = &E.qf.items[idx];
        // if (idx == E.qf.sel) { memcpy(&ab[*ablen], "\x1b[7m", 4); *ablen += 4; }
        char line[512];
        int l = 0;
        if (it->filename && it->filename[0]) l = snprintf(line, sizeof(line), "%s:%d:%d: %s", it->filename, it->line, it->col, it->text ? it->text : "");
        else l = snprintf(line, sizeof(line), "%d:%d: %s", it->line, it->col, it->text ? it->text : "");
        if (l > width) l = width;
        if (l > 0) { memcpy(&ab[*ablen], line, l); *ablen += l; }
        if (idx == E.qf.sel) { memcpy(&ab[*ablen], "\x1b[m", 3); *ablen += 3; }
        memcpy(&ab[*ablen], "\x1b[K\r\n", 5); *ablen += 5;
    }
}

void buf_refresh_screen(void) {
    /* Adjust available content rows based on message height. */
    int needed = ed_message_lines_needed();
    int old_rows = E.screen_rows;
    /* Max message lines that fit: old_rows (content) + 1 (message default) */
    int max_msg = old_rows + 1;
    if (needed > max_msg) needed = max_msg;
    g_msg_lines_current = needed;
    int avail_rows = old_rows - (needed - 1);
    if (E.qf.open && E.qf.height > 0) {
        avail_rows -= E.qf.height;
    }
    if (avail_rows < 1) avail_rows = 1;
    E.screen_rows = avail_rows;

    buf_scroll();

    char ab[8192];
    int ablen = 0;

    memcpy(&ab[ablen], "\x1b[?25l", 6);
    ablen += 6;
    memcpy(&ab[ablen], "\x1b[H", 3);
    ablen += 3;

    ed_draw_rows(ab, &ablen, sizeof(ab));
    ed_draw_status_bar(ab, &ablen, sizeof(ab));
    if (E.qf.open && E.qf.height > 0) {
        ed_draw_quickfix(ab, &ablen, sizeof(ab));
    }
    ed_draw_message_bar(ab, &ablen, sizeof(ab));

    Buffer *buf = buf_cur();
    char cursor_buf[32];
    int gutter = ln_gutter_width();
    int margin = gutter ? (gutter + 1) : 0;
    int cur_row = (buf ? buf->cursor_y - buf->row_offset : 0) + 1;
    int cur_col = (E.render_x - (buf ? buf->col_offset : 0)) + 1 + margin;
    if (E.qf.open && E.qf.focus) {
        cur_row = E.screen_rows + 2; /* status bar is one line; quickfix header is next */
        cur_col = 1;
    }
    snprintf(cursor_buf, sizeof(cursor_buf), "\x1b[%d;%dH", cur_row, cur_col);
    memcpy(&ab[ablen], cursor_buf, strlen(cursor_buf));
    ablen += strlen(cursor_buf);

    memcpy(&ab[ablen], "\x1b[?25h", 6);
    ablen += 6;

    write(STDOUT_FILENO, ab, ablen);

    /* Restore content rows */
    E.screen_rows = old_rows;
}

void ed_set_status_message(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.status_msg, sizeof(E.status_msg), fmt, ap);
    va_end(ap);
}
