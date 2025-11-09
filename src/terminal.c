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

void ed_draw_rows(char *ab, int *ablen, int maxlen) {
    Buffer *buf = buf_cur();

    for (int y = 0; y < E.screen_rows; y++) {
        int filerow = y + (buf ? buf->row_offset : 0);
        if (!buf || filerow >= buf->num_rows) {
            if ((!buf || buf->num_rows == 0) && y == E.screen_rows / 3) {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                    "Hed editor -- version %s", HED_VERSION);
                if (welcomelen > E.screen_cols) welcomelen = E.screen_cols;
                int padding = (E.screen_cols - welcomelen) / 2;
                if (padding) {
                    ab[(*ablen)++] = '~';
                    padding--;
                }
                while (padding--) ab[(*ablen)++] = ' ';
                memcpy(&ab[*ablen], welcome, welcomelen);
                *ablen += welcomelen;
            } else {
                ab[(*ablen)++] = '~';
            }
        } else {
            int len = buf->rows[filerow].render.len - buf->col_offset;
            if (len < 0) len = 0;
            if (len > E.screen_cols) len = E.screen_cols;
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
    Buffer *buf = buf_cur();

    memcpy(&ab[*ablen], "\x1b[7m", 4);
    *ablen += 4;

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
        buf && buf->dirty ? "(modified) " : "", mode_str);
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
    memcpy(&ab[*ablen], "\x1b[m", 3);
    *ablen += 3;
    ab[(*ablen)++] = '\r';
    ab[(*ablen)++] = '\n';
    (void)maxlen;
}

void ed_draw_message_bar(char *ab, int *ablen, int maxlen) {
    memcpy(&ab[*ablen], "\x1b[K", 3);
    *ablen += 3;

    if (E.mode == MODE_COMMAND) {
        ab[(*ablen)++] = ':';
        int msglen = E.command_len;
        if (msglen > E.screen_cols - 1) msglen = E.screen_cols - 1;
        if (msglen > 0) {
            memcpy(&ab[*ablen], E.command_buf, msglen);
            *ablen += msglen;
        }
    } else {
        int msglen = strlen(E.status_msg);
        if (msglen > E.screen_cols) msglen = E.screen_cols;
        memcpy(&ab[*ablen], E.status_msg, msglen);
        *ablen += msglen;
    }
    (void)maxlen;
}

void buf_refresh_screen(void) {
    buf_scroll();

    char ab[8192];
    int ablen = 0;

    memcpy(&ab[ablen], "\x1b[?25l", 6);
    ablen += 6;
    memcpy(&ab[ablen], "\x1b[H", 3);
    ablen += 3;

    ed_draw_rows(ab, &ablen, sizeof(ab));
    ed_draw_status_bar(ab, &ablen, sizeof(ab));
    ed_draw_message_bar(ab, &ablen, sizeof(ab));

    Buffer *buf = buf_cur();
    char cursor_buf[32];
    snprintf(cursor_buf, sizeof(cursor_buf), "\x1b[%d;%dH",
        (buf ? buf->cursor_y - buf->row_offset : 0) + 1,
        (E.render_x - (buf ? buf->col_offset : 0)) + 1);
    memcpy(&ab[ablen], cursor_buf, strlen(cursor_buf));
    ablen += strlen(cursor_buf);

    memcpy(&ab[ablen], "\x1b[?25h", 6);
    ablen += 6;

    write(STDOUT_FILENO, ab, ablen);
}

void ed_set_status_message(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.status_msg, sizeof(E.status_msg), fmt, ap);
    va_end(ap);
}
