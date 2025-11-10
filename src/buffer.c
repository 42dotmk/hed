#include "hed.h"

/*** Buffer Management ***/

Buffer *buf_cur(void) {
    if (E.num_buffers == 0) return NULL;
    return &E.buffers[E.current_buffer];
}

char *buf_detect_filetype(const char *filename) {
    if (!filename) return strdup("txt");

    const char *ext = strrchr(filename, '.');
    if (!ext || ext == filename) return strdup("txt");

    ext++; /* Skip the dot */

    /* Common filetypes */
    if (strcmp(ext, "c") == 0 || strcmp(ext, "h") == 0) return strdup("c");
    if (strcmp(ext, "cpp") == 0 || strcmp(ext, "cc") == 0 || strcmp(ext, "cxx") == 0) return strdup("cpp");
    if (strcmp(ext, "hpp") == 0 || strcmp(ext, "hh") == 0 || strcmp(ext, "hxx") == 0) return strdup("cpp");
    if (strcmp(ext, "py") == 0) return strdup("python");
    if (strcmp(ext, "js") == 0) return strdup("javascript");
    if (strcmp(ext, "ts") == 0) return strdup("typescript");
    if (strcmp(ext, "java") == 0) return strdup("java");
    if (strcmp(ext, "rs") == 0) return strdup("rust");
    if (strcmp(ext, "go") == 0) return strdup("go");
    if (strcmp(ext, "sh") == 0) return strdup("shell");
    if (strcmp(ext, "md") == 0) return strdup("markdown");
    if (strcmp(ext, "html") == 0 || strcmp(ext, "htm") == 0) return strdup("html");
    if (strcmp(ext, "css") == 0) return strdup("css");
    if (strcmp(ext, "json") == 0) return strdup("json");
    if (strcmp(ext, "xml") == 0) return strdup("xml");
    if (strcmp(ext, "txt") == 0) return strdup("txt");

    /* Default: use the extension as-is */
    return strdup(ext);
}

void buf_init(Buffer *buf) {
    buf->rows = NULL;
    buf->num_rows = 0;
    buf->cursor_x = 0;
    buf->cursor_y = 0;
    buf->row_offset = 0;
    buf->col_offset = 0;
    buf->filename = NULL;
    buf->filetype = NULL;
    buf->dirty = 0;
    buf->visual_start_x = 0;
    buf->visual_start_y = 0;
}

int buf_new(char *filename) {
    if (E.num_buffers >= MAX_BUFFERS) {
        ed_set_status_message("Maximum buffers reached");
        return -1;
    }

    int idx = E.num_buffers++;
    buf_init(&E.buffers[idx]);

    if (filename) {
        E.buffers[idx].filename = strdup(filename);
    }

    /* Detect and set filetype */
    E.buffers[idx].filetype = buf_detect_filetype(filename);

    return idx;
}

void buf_switch(int index) {
    if (index < 0 || index >= E.num_buffers) {
        ed_set_status_message("Invalid buffer index");
        return;
    }
    E.current_buffer = index;
    Buffer *buf = buf_cur();

    /* Fire hook */
    HookBufferEvent event = {buf, buf->filename};
    hook_fire_buffer(HOOK_BUFFER_SWITCH, &event);

    ed_set_status_message("Switched to buffer %d: %s", index + 1,
        buf->filename ? buf->filename : "[No Name]");
}

void buf_next(void) {
    if (E.num_buffers <= 1) return;
    E.current_buffer = (E.current_buffer + 1) % E.num_buffers;
    Buffer *buf = buf_cur();

    /* Fire hook */
    HookBufferEvent event = {buf, buf->filename};
    hook_fire_buffer(HOOK_BUFFER_SWITCH, &event);

    ed_set_status_message("Buffer %d: %s", E.current_buffer + 1,
        buf->filename ? buf->filename : "[No Name]");
}

void buf_prev(void) {
    if (E.num_buffers <= 1) return;
    E.current_buffer = (E.current_buffer - 1 + E.num_buffers) % E.num_buffers;
    Buffer *buf = buf_cur();

    /* Fire hook */
    HookBufferEvent event = {buf, buf->filename};
    hook_fire_buffer(HOOK_BUFFER_SWITCH, &event);

    ed_set_status_message("Buffer %d: %s", E.current_buffer + 1,
        buf->filename ? buf->filename : "[No Name]");
}

void buf_close(int index) {
    if (index < 0 || index >= E.num_buffers) {
        ed_set_status_message("Invalid buffer index");
        return;
    }

    Buffer *buf = &E.buffers[index];

    if (buf->dirty) {
        ed_set_status_message("Buffer has unsaved changes! Save first or use :bd!");
        return;
    }

    /* Fire hook before closing */
    HookBufferEvent event = {buf, buf->filename};
    hook_fire_buffer(HOOK_BUFFER_CLOSE, &event);

    /* Free buffer resources */
    for (int i = 0; i < buf->num_rows; i++) {
        buf_row_free(&buf->rows[i]);
    }
    free(buf->rows);
    free(buf->filename);
    free(buf->filetype);

    /* Shift buffers down */
    for (int i = index; i < E.num_buffers - 1; i++) {
        E.buffers[i] = E.buffers[i + 1];
    }
    E.num_buffers--;

    if (E.num_buffers == 0) {
        /* Create an empty buffer */
        buf_new(NULL);
        E.current_buffer = 0;
    } else if (E.current_buffer >= E.num_buffers) {
        E.current_buffer = E.num_buffers - 1;
    }

    ed_set_status_message("Buffer closed");
}

void buf_list(void) {
    char msg[256] = "Buffers: ";
    for (int i = 0; i < E.num_buffers; i++) {
        char buf_info[64];
        snprintf(buf_info, sizeof(buf_info), "[%d]%s%s ",
            i + 1,
            i == E.current_buffer ? "*" : "",
            E.buffers[i].filename ? E.buffers[i].filename : "[No Name]");
        if (strlen(msg) + strlen(buf_info) < sizeof(msg) - 1) {
            strcat(msg, buf_info);
        }
    }
    ed_set_status_message("%s", msg);
}

/*** Row operations ***/

int buf_row_cx_to_rx(const Row *row, int cx) {
    int rx = 0;
    for (int j = 0; j < cx; j++) {
        if (j < (int)row->chars.len && row->chars.data[j] == '\t')
            rx += (TAB_STOP - 1) - (rx % TAB_STOP);
        rx++;
    }
    return rx;
}

int buf_row_rx_to_cx(const Row *row, int rx) {
    int cur_rx = 0;
    int cx;
    for (cx = 0; cx < (int)row->chars.len; cx++) {
        if (row->chars.data[cx] == '\t')
            cur_rx += (TAB_STOP - 1) - (cur_rx % TAB_STOP);
        cur_rx++;
        if (cur_rx > rx) return cx;
    }
    return cx;
}

void buf_row_update(Row *row) {
    int tabs = 0;
    for (size_t j = 0; j < row->chars.len; j++)
        if (row->chars.data[j] == '\t') tabs++;

    sstr_free(&row->render);
    row->render = sstr_new();
    sstr_reserve(&row->render, row->chars.len + tabs * (TAB_STOP - 1) + 1);

    for (size_t j = 0; j < row->chars.len; j++) {
        if (row->chars.data[j] == '\t') {
            sstr_append_char(&row->render, ' ');
            while (row->render.len % TAB_STOP != 0)
                sstr_append_char(&row->render, ' ');
        } else {
            sstr_append_char(&row->render, row->chars.data[j]);
        }
    }
}

void buf_row_insert(int at, const char *s, size_t len) {
    Buffer *buf = buf_cur();
    if (!buf) return;
    if (at < 0 || at > buf->num_rows) return;

    buf->rows = realloc(buf->rows, sizeof(Row) * (buf->num_rows + 1));
    memmove(&buf->rows[at + 1], &buf->rows[at], sizeof(Row) * (buf->num_rows - at));

    buf->rows[at].chars = sstr_from(s, len);
    buf->rows[at].render = sstr_new();
    buf_row_update(&buf->rows[at]);

    buf->num_rows++;
    buf->dirty++;

    /* Fire hook */
    HookLineEvent event = {buf, at, s, len};
    hook_fire_line(HOOK_LINE_INSERT, &event);
}

void buf_row_free(Row *row) {
    sstr_free(&row->chars);
    sstr_free(&row->render);
}

void buf_row_del(int at) {
    Buffer *buf = buf_cur();
    if (!buf) return;
    if (at < 0 || at >= buf->num_rows) return;
    buf_row_free(&buf->rows[at]);
    memmove(&buf->rows[at], &buf->rows[at + 1], sizeof(Row) * (buf->num_rows - at - 1));
    buf->num_rows--;
    buf->dirty++;
}

void buf_row_insert_char(Row *row, int at, int c) {
    Buffer *buf = buf_cur();
    if (!buf) return;
    sstr_insert_char(&row->chars, at, c);
    buf_row_update(row);
    buf->dirty++;
}

void buf_row_append(Row *row, const SizedStr *str) {
    Buffer *buf = buf_cur();
    if (!buf) return;
    sstr_append(&row->chars, str->data, str->len);
    buf_row_update(row);
    buf->dirty++;
}

void buf_row_del_char(Row *row, int at) {
    Buffer *buf = buf_cur();
    if (!buf) return;
    if (at < 0 || at >= (int)row->chars.len) return;
    sstr_delete_char(&row->chars, at);
    buf_row_update(row);
    buf->dirty++;
}

void buf_insert_char(int c) {
    Buffer *buf = buf_cur();
    if (!buf) return;
    if (buf->cursor_y == buf->num_rows) {
        buf_row_insert(buf->num_rows, "", 0);
    }
    int y0 = buf->cursor_y; int x0 = buf->cursor_x;
    if (!undo_is_applying()) {
        if (E.mode == MODE_INSERT) undo_open_insert_group(); else undo_begin_group();
        char ch = (char)c;
        undo_push_insert(y0, x0, &ch, 1, y0, x0, y0, x0 + 1);
    }
    buf_row_insert_char(&buf->rows[y0], x0, c);

    /* Fire hook */
    HookCharEvent event = {buf, y0, x0, c};
    hook_fire_char(HOOK_CHAR_INSERT, &event);

    buf->cursor_x = x0 + 1;
}

void buf_insert_newline(void) {
    Buffer *buf = buf_cur();
    if (!buf) return;
    int y0 = buf->cursor_y; int x0 = buf->cursor_x;
    if (!undo_is_applying()) {
        if (E.mode == MODE_INSERT) undo_open_insert_group(); else undo_begin_group();
        const char nl = '\n';
        undo_push_insert(y0, x0, &nl, 1, y0, x0, y0 + 1, 0);
    }
    if (x0 == 0) {
        buf_row_insert(buf->cursor_y, "", 0);
    } else {
        Row *row = &buf->rows[y0];
        const char *rest = row->chars.data + x0;
        size_t rest_len = row->chars.len - x0;
        buf_row_insert(y0 + 1, rest, rest_len);

        row = &buf->rows[y0];
        row->chars.len = x0;
        row->chars.data[row->chars.len] = '\0';
        buf_row_update(row);
    }
    buf->cursor_y = y0 + 1;
    buf->cursor_x = 0;
}

void buf_del_char(void) {
    Buffer *buf = buf_cur();
    if (!buf) return;
    if (buf->cursor_y == buf->num_rows) return;
    if (buf->cursor_x == 0 && buf->cursor_y == 0) return;

    int y = buf->cursor_y; int x = buf->cursor_x;
    Row *row = &buf->rows[y];
    if (x > 0) {
        int deleted_char = (x - 1 < (int)row->chars.len) ? row->chars.data[x - 1] : 0;
        if (!undo_is_applying()) {
            char ch = (char)deleted_char;
            undo_begin_group();
            undo_push_delete(y, x - 1, &ch, 1, y, x, y, x - 1);
        }
        buf_row_del_char(row, x - 1);

        /* Fire hook */
        HookCharEvent event = {buf, y, x - 1, deleted_char};
        hook_fire_char(HOOK_CHAR_DELETE, &event);

        buf->cursor_x = x - 1;
    } else {
        int prev_len = buf->rows[y - 1].chars.len;
        if (!undo_is_applying()) {
            const char nl = '\n';
            undo_begin_group();
            undo_push_delete(y - 1, prev_len, &nl, 1, y, x, y - 1, prev_len);
        }
        buf->cursor_x = prev_len;
        buf_row_append(&buf->rows[y - 1], &row->chars);
        buf_row_del(y);
        buf->cursor_y = y - 1;
    }
}

void buf_delete_line(void) {
    Buffer *buf = buf_cur();
    if (!buf) return;
    if (buf->cursor_y >= buf->num_rows) return;

    /* Save to clipboard */
    sstr_free(&E.clipboard);
    E.clipboard = sstr_from(buf->rows[buf->cursor_y].chars.data,
                            buf->rows[buf->cursor_y].chars.len);
    /* Update registers: numbered delete and unnamed */
    regs_push_delete(buf->rows[buf->cursor_y].chars.data,
                     buf->rows[buf->cursor_y].chars.len);

    /* Fire hook before deletion */
    HookLineEvent event = {buf, buf->cursor_y, buf->rows[buf->cursor_y].chars.data,
                          buf->rows[buf->cursor_y].chars.len};
    hook_fire_line(HOOK_LINE_DELETE, &event);

    if (!undo_is_applying()) {
        int y0 = buf->cursor_y;
        SizedStr cap = sstr_new();
        sstr_append(&cap, buf->rows[y0].chars.data, buf->rows[y0].chars.len);
        sstr_append_char(&cap, '\n');
        undo_begin_group();
        undo_push_delete(y0, 0, cap.data, cap.len, y0, 0, y0, 0);
        sstr_free(&cap);
    }

    buf_row_del(buf->cursor_y);
    if (buf->cursor_y >= buf->num_rows && buf->num_rows > 0)
        buf->cursor_y = buf->num_rows - 1;
    buf->cursor_x = 0;
}
void buf_yank_line(void) {
    Buffer *buf = buf_cur();
    if (!buf) return;
    if (buf->cursor_y >= buf->num_rows) return;

    sstr_free(&E.clipboard);
    E.clipboard = sstr_from(buf->rows[buf->cursor_y].chars.data,
                            buf->rows[buf->cursor_y].chars.len);
    /* Update registers: yank '0' and unnamed */
    regs_set_yank(buf->rows[buf->cursor_y].chars.data,
                  buf->rows[buf->cursor_y].chars.len);
}

void buf_paste(void) {
    Buffer *buf = buf_cur();
    if (!buf) return;
    if (E.clipboard.len == 0) return;

    int at = (buf->cursor_y < buf->num_rows) ? (buf->cursor_y + 1) : buf->num_rows;
    if (!undo_is_applying()) {
        undo_begin_group();
        undo_push_insert(at, 0, E.clipboard.data, E.clipboard.len,
                         buf->cursor_y, buf->cursor_x, at, 0);
    }
    if (buf->cursor_y < buf->num_rows) {
        buf->cursor_y++;
    }
    buf_row_insert(buf->cursor_y, E.clipboard.data, E.clipboard.len);
    buf->cursor_x = 0;
}

/*** Search ***/

void buf_find(void) {
    Buffer *buf = buf_cur();
    if (!buf) return;
    if (E.search_query.len == 0) return;

    int start_y = buf->cursor_y;
    int current = start_y;

    for (int i = 0; i < buf->num_rows; i++) {
        current = (start_y + i + 1) % buf->num_rows;
        Row *row = &buf->rows[current];
        char *match = strstr(row->render.data, E.search_query.data);
        if (match) {
            buf->cursor_y = current;
            buf->cursor_x = buf_row_rx_to_cx(row, match - row->render.data);
            buf->row_offset = buf->num_rows;
            ed_set_status_message("Found at line %d", current + 1);
            return;
        }
    }

    ed_set_status_message("Not found: %s", E.search_query.data);
}

/* Reload current buffer's file from disk, discarding unsaved changes */
void buf_reload_current(void) {
    Buffer *buf = buf_cur();
    if (!buf || !buf->filename) {
        ed_set_status_message("reload: no file");
        return;
    }
    /* Clear existing rows */
    for (int i = 0; i < buf->num_rows; i++) {
        buf_row_free(&buf->rows[i]);
    }
    free(buf->rows);
    buf->rows = NULL;
    buf->num_rows = 0;
    buf->row_offset = buf->col_offset = 0;
    buf->cursor_x = buf->cursor_y = 0;

    /* Detect filetype (update) */
    free(buf->filetype);
    buf->filetype = buf_detect_filetype(buf->filename);

    FILE *fp = fopen(buf->filename, "r");
    if (!fp) {
        ed_set_status_message("reload: cannot open %s", buf->filename);
        buf->dirty = 0;
        return;
    }
    char *line = NULL; size_t cap = 0; ssize_t len;
    while ((len = getline(&line, &cap, fp)) != -1) {
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) len--;
        buf_row_insert(buf->num_rows, line, (size_t)len);
    }
    free(line);
    fclose(fp);
    buf->dirty = 0;
    ed_set_status_message("reloaded: %s", buf->filename);
}
