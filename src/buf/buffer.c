#include "hed.h"
#include "safe_string.h"

/* Internal low-level row helpers (not part of public API) */
void buf_row_insert_in(Buffer *buf, int at, const char *s, size_t len);
void buf_row_del_in(Buffer *buf, int at);
void buf_row_insert_char_in(Buffer *buf, Row *row, int at, int c);
void buf_row_append_in(Buffer *buf, Row *row, const SizedStr *str);
void buf_row_del_char_in(Buffer *buf, Row *row, int at);

Buffer *buf_cur(void) {
    if (E.buffers.len == 0) return NULL;
    return &E.buffers.data[E.current_buffer];
}

int buf_find_by_filename(const char *filename) {
    if (!filename) return -1;
    for (int i = 0; i < (int)E.buffers.len; i++) {
        if (E.buffers.data[i].filename && strcmp(E.buffers.data[i].filename, filename) == 0) {
            return i;
        }
    }
    return -1;
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

/* Initialize a Buffer struct in-place with default values */
static void buf_init(Buffer *buf) {
    if (!buf) return;
    buf->rows = NULL;
    buf->num_rows = 0;
    buf->cursor.x = 0;
    buf->cursor.y = 0;
    buf->filename = NULL;
    buf->title = strdup("[No Name]");
    if (!buf->title) buf->title = NULL;  /* Handle OOM gracefully */
    buf->filetype = NULL;
    buf->dirty = 0;
    buf->readonly = 0;  /* Default: not read-only */
    buf->ts_internal = NULL;
}

/* Create a new buffer and return EdError status */
EdError buf_new(const char *filename, int *out_idx) {
    if (!PTR_VALID(out_idx)) return ED_ERR_INVALID_ARG;
    *out_idx = -1;

    /* Ensure capacity for new buffer */
    if (!vec_reserve_typed(&E.buffers, E.buffers.len + 1, sizeof(Buffer))) {
        return ED_ERR_NOMEM;
    }

    int idx = E.buffers.len++;
    Buffer *buf = &E.buffers.data[idx];
    buf_init(buf);

    /* Apply filename/title and detect filetype */
    if (filename && *filename) {
        /* Replace default title */
        free(buf->title);
        buf->title = strdup(filename);
        if (!buf->title) {
            /* OOM on title allocation - cleanup and fail */
            buf->title = NULL;
            E.buffers.len--;  /* Rollback buffer creation */
            return ED_ERR_NOMEM;
        }

        buf->filename = strdup(filename);
        if (!buf->filename) {
            /* OOM on filename allocation - cleanup and fail */
            free(buf->title);
            buf->title = NULL;
            E.buffers.len--;  /* Rollback buffer creation */
            return ED_ERR_NOMEM;
        }
    }

    buf->filetype = buf_detect_filetype(filename);
    if (!buf->filetype) {
        /* OOM on filetype allocation - cleanup and fail */
        free(buf->title);
        free(buf->filename);
        E.buffers.len--;  /* Rollback buffer creation */
        return ED_ERR_NOMEM;
    }

    *out_idx = idx;
    return ED_OK;
}

/* Create the special *messages buffer and return EdError status */
EdError buf_new_messages(int *out_idx) {
    if (!PTR_VALID(out_idx)) return ED_ERR_INVALID_ARG;
    *out_idx = -1;

    /* Ensure capacity for messages buffer */
    if (!vec_reserve_typed(&E.buffers, E.buffers.len + 1, sizeof(Buffer))) {
        return ED_ERR_NOMEM;
    }

    int idx = E.buffers.len++;
    Buffer *buf = &E.buffers.data[idx];
    buf_init(buf);

    /* Set special properties for *messages buffer */
    free(buf->title);
    buf->title = strdup("*messages");
    if (!buf->title) {
        /* Critical OOM - can't start without messages buffer */
        buf->title = NULL;
        E.buffers.len--;
        return ED_ERR_NOMEM;
    }

    buf->filename = NULL;
    buf->readonly = 1;  /* Make it read-only */
    buf->dirty = 0;

    /* Add initial message */
    buf_row_insert_in(buf, 0, "--- Messages ---", 16);

    *out_idx = idx;
    return ED_OK;
}

/* Opens a file and returns EdError status */
EdError buf_open_file(const char *filename, Buffer **out) {
    if (!PTR_VALID(out)) return ED_ERR_INVALID_ARG;
    *out = NULL;
    if (!PTR_VALID(filename)) return ED_ERR_INVALID_ARG;

    int idx;
    EdError err = buf_new(filename, &idx);
    if (err != ED_OK) return err;
    Buffer *buf = &E.buffers.data[idx];

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        /* New file - this is OK, not an error */
        ed_set_status_message("New file: %s", filename);
        *out = buf;
        return ED_OK;
    }

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 && (line[linelen - 1] == '\n' ||
                               line[linelen - 1] == '\r'))
            linelen--;
        buf_row_insert_in(buf, buf->num_rows, line, linelen);
    }
    free(line);
    fclose(fp);
    buf->dirty = 0;

    recent_files_add(&E.recent_files, filename);
    HookBufferEvent event = {buf, filename};
    hook_fire_buffer(HOOK_BUFFER_OPEN, &event);

    ed_set_status_message("Loaded: %s", filename);
    if (ts_is_enabled()) {
        ts_buffer_autoload(buf);
        ts_buffer_reparse(buf);
    }
    *out = buf;
    return ED_OK;
}

void buf_open_or_switch(const char *filename) {
    if (!filename || !*filename) {
        ed_set_status_message("No filename provided");
        return;
    }

    /* Check if buffer already exists */
    int found = buf_find_by_filename(filename);
    if (found >= 0) {
        /* Switch to existing buffer and reload it */
        EdError err = buf_switch(found);
        if (err == ED_OK) {
            buf_reload(buf_cur());
            ed_set_status_message("Switched to: %s (reloaded)", filename);
        } else {
            ed_set_status_message("Failed to switch: %s", ed_error_string(err));
        }
    } else {
        /* Open new buffer and attach to current window */
        Buffer *nb = NULL;
        EdError err = buf_open_file(filename, &nb);
        if (err == ED_OK || err == ED_ERR_FILE_NOT_FOUND) {
            /* Success or new file (both OK) */
            Window *win = window_cur();
            if (win && nb) {
                win_attach_buf(win, nb);
            }
            ed_set_status_message("Opened: %s", filename);
        } else {
            ed_set_status_message("Failed to open: %s", ed_error_string(err));
        }
    }
}

/* Switch to a buffer by index and return EdError status */
EdError buf_switch(int index) {
    if (!BOUNDS_CHECK(index, E.buffers.len)) {
        return ED_ERR_INVALID_INDEX;
    }

    /* Record current position before switching */
    Window *win = window_cur();
    if (win) {
        jump_list_add(&E.jump_list, E.current_buffer, win->cursor.x, win->cursor.y);
    }

    E.current_buffer = index;
    if (win) win->buffer_index = index;
    Buffer *buf = buf_cur();

    /* Fire hook */
    HookBufferEvent event = {buf, buf->filename};
    hook_fire_buffer(HOOK_BUFFER_SWITCH, &event);

    return ED_OK;
}

void buf_next(void) {
    if (E.buffers.len <= 1) return;

    /* Record current position before switching */
    Window *win = window_cur();
    if (win) {
        jump_list_add(&E.jump_list, E.current_buffer, win->cursor.x, win->cursor.y);
    }

    E.current_buffer = (E.current_buffer + 1) % E.buffers.len;
    if (win) win->buffer_index = E.current_buffer;
    Buffer *buf = buf_cur();

    /* Fire hook */
    HookBufferEvent event = {buf, buf->filename};
    hook_fire_buffer(HOOK_BUFFER_SWITCH, &event);

    ed_set_status_message("Buffer %d: %s", E.current_buffer + 1, buf->title);
}

void buf_prev(void) {
    if (E.buffers.len <= 1) return;

    /* Record current position before switching */
    Window *win = window_cur();
    if (win) {
        jump_list_add(&E.jump_list, E.current_buffer, win->cursor.x, win->cursor.y);
    }

    E.current_buffer = (E.current_buffer - 1 + E.buffers.len) % E.buffers.len;
    if (win) win->buffer_index = E.current_buffer;
    Buffer *buf = buf_cur();

    /* Fire hook */
    HookBufferEvent event = {buf, buf->filename};
    hook_fire_buffer(HOOK_BUFFER_SWITCH, &event);

    ed_set_status_message("Buffer %d: %s", E.current_buffer + 1, buf->title);
}

/* Close a buffer by index and return EdError status */
EdError buf_close(int index) {
    if (!BOUNDS_CHECK(index, E.buffers.len)) {
        return ED_ERR_INVALID_INDEX;
    }

    /* Prevent closing the *messages buffer */
    if (index == E.messages_buffer_index) {
        return ED_ERR_BUFFER_READONLY;
    }

    Buffer *buf = &E.buffers.data[index];

    if (buf->dirty) {
        return ED_ERR_BUFFER_DIRTY;
    }

    /* Fire hook before closing */
    HookBufferEvent event = {buf, buf->filename};
    hook_fire_buffer(HOOK_BUFFER_CLOSE, &event);

    /* Free buffer resources */
    /* Tree-sitter cleanup (no-op if disabled) */
    ts_buffer_free(buf);
    for (int i = 0; i < buf->num_rows; i++) {
        row_free(&buf->rows[i]);
    }
    free(buf->rows);
    free(buf->filename);
    free(buf->title);
    free(buf->filetype);

    /* Shift buffers down */
    for (int i = index; i < (int)E.buffers.len - 1; i++) {
        E.buffers.data[i] = E.buffers.data[i + 1];
    }
    E.buffers.len--;

    if (E.buffers.len == 0) {
        /* Create an empty buffer */
        int idx;
        EdError err = buf_new(NULL, &idx);
        if (err == ED_OK) {
            E.current_buffer = 0;
        }
        /* If buffer creation fails, editor will be in an invalid state, but better than crashing */
    } else if (E.current_buffer >= (int)E.buffers.len) {
        E.current_buffer = (int)E.buffers.len - 1;
    }

    return ED_OK;
}

void buf_append_message(const char *msg) {
    if (E.messages_buffer_index < 0 || E.messages_buffer_index >= (int)E.buffers.len) {
        return;  /* Messages buffer not initialized or invalid */
    }

    Buffer *mbuf = &E.buffers.data[E.messages_buffer_index];

    /* Temporarily make it writable */
    int was_readonly = mbuf->readonly;
    mbuf->readonly = 0;

    /* Append the message as a new row into the messages buffer */
    buf_row_insert_in(mbuf, mbuf->num_rows, msg, strlen(msg));

    /* Restore readonly status */
    mbuf->readonly = was_readonly;
    mbuf->dirty = 0;  /* Messages buffer is never "dirty" */
}

/* buf_list was unused; removed. */

/*** Row operations ***/



/* Insert a row into a specific buffer (no window/state changes) */
void buf_row_insert_in(Buffer *buf, int at, const char *s, size_t len) {
    if (!buf) return;
    if (at < 0 || at > buf->num_rows) return;

    Row *new_rows = realloc(buf->rows, sizeof(Row) * (buf->num_rows + 1));
    if (!new_rows) {
        ed_set_status_message("Out of memory");
        return;
    }
    buf->rows = new_rows;
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



void buf_row_del_in(Buffer *buf, int at) {
    if (!PTR_VALID(buf)) return;
    if (!BOUNDS_CHECK(at, buf->num_rows)) return;
    row_free(&buf->rows[at]);
    memmove(&buf->rows[at], &buf->rows[at + 1], sizeof(Row) * (buf->num_rows - at - 1));
    buf->num_rows--;
    buf->dirty++;
}


void buf_row_insert_char_in(Buffer *buf, Row *row, int at, int c) {
    if (!buf || !row) return;
    sstr_insert_char(&row->chars, at, c);
    buf_row_update(row);
    buf->dirty++;
}

/* Legacy wrapper removed; use buf_row_insert_char_in */

void buf_row_append_in(Buffer *buf, Row *row, const SizedStr *str) {
    if (!buf || !row || !str) return;
    sstr_append(&row->chars, str->data, str->len);
    buf_row_update(row);
    buf->dirty++;
}


void buf_row_del_char_in(Buffer *buf, Row *row, int at) {
    if (!buf || !row) return;
    if (at < 0 || at >= (int)row->chars.len) return;
    sstr_delete_char(&row->chars, at);
    buf_row_update(row);
    buf->dirty++;
}


void buf_insert_char_in(Buffer *buf, int c) {
    Window *win = window_cur();
    if (!buf || !win) return;
    if (buf->readonly) {
        ed_set_status_message("Buffer is read-only");
        return;
    }
    if (win->cursor.y == buf->num_rows) {
        buf_row_insert_in(buf, buf->num_rows, "", 0);
    }
    int y0 = win->cursor.y; int x0 = win->cursor.x;
    if (!undo_is_applying()) {
        if (E.mode == MODE_INSERT) undo_open_insert_group(); else undo_begin_group();
        char ch = (char)c;
        undo_push_insert(y0, x0, &ch, 1, y0, x0, y0, x0 + 1);
    }
    buf_row_insert_char_in(buf, &buf->rows[y0], x0, c);

    /* Fire hook */
    HookCharEvent event = {buf, y0, x0, c};
    hook_fire_char(HOOK_CHAR_INSERT, &event);

    win->cursor.x = x0 + 1;
}

void buf_insert_newline_in(Buffer *buf) {
    Window *win = window_cur();
    if (!buf || !win) return;
    if (buf->readonly) {
        ed_set_status_message("Buffer is read-only");
        return;
    }
    int y0 = win->cursor.y; int x0 = win->cursor.x;
    if (!undo_is_applying()) {
        if (E.mode == MODE_INSERT) undo_open_insert_group(); else undo_begin_group();
        const char nl = '\n';
        undo_push_insert(y0, x0, &nl, 1, y0, x0, y0 + 1, 0);
    }
    if (x0 == 0) {
        buf_row_insert_in(buf, win->cursor.y, "", 0);
    } else {
        Row *row = &buf->rows[y0];
        const char *rest = row->chars.data + x0;
        size_t rest_len = row->chars.len - x0;
        buf_row_insert_in(buf, y0 + 1, rest, rest_len);

        row = &buf->rows[y0];
        row->chars.len = x0;
        row->chars.data[row->chars.len] = '\0';
        buf_row_update(row);
    }
    win->cursor.y = y0 + 1;
    win->cursor.x = 0;
}

void buf_del_char_in(Buffer *buf) {
    Window *win = window_cur();
    if (!buf || !win) return;
    if (buf->readonly) {
        ed_set_status_message("Buffer is read-only");
        return;
    }
    if (win->cursor.y == buf->num_rows) return;
    if (win->cursor.x == 0 && win->cursor.y == 0) return;

    int y = win->cursor.y; int x = win->cursor.x;
    Row *row = &buf->rows[y];
    if (x > 0) {
        int deleted_char = (x - 1 < (int)row->chars.len) ? row->chars.data[x - 1] : 0;
        if (!undo_is_applying()) {
            char ch = (char)deleted_char;
            undo_begin_group();
            undo_push_delete(y, x - 1, &ch, 1, y, x, y, x - 1);
        }
        buf_row_del_char_in(buf, row, x - 1);

        /* Fire hook */
        HookCharEvent event = {buf, y, x - 1, deleted_char};
        hook_fire_char(HOOK_CHAR_DELETE, &event);

        win->cursor.x = x - 1;
    } else {
        int prev_len = buf->rows[y - 1].chars.len;
        if (!undo_is_applying()) {
            const char nl = '\n';
            undo_begin_group();
            undo_push_delete(y - 1, prev_len, &nl, 1, y, x, y - 1, prev_len);
        }
        win->cursor.x = prev_len;
        buf_row_append_in(buf, &buf->rows[y - 1], &row->chars);
        buf_row_del_in(buf, y);
        win->cursor.y = y - 1;
    }
}

void buf_delete_line_in(Buffer *buf) {
    Window *win = window_cur();
    if (!buf || !win) return;
    if (buf->readonly) {
        ed_set_status_message("Buffer is read-only");
        return;
    }
    if (!BOUNDS_CHECK(win->cursor.y, buf->num_rows)) return;

    /* Save to clipboard */
    sstr_free(&E.clipboard);
    E.clipboard = sstr_from(buf->rows[win->cursor.y].chars.data,
                            buf->rows[win->cursor.y].chars.len);
    /* Update registers: numbered delete and unnamed */
    regs_push_delete(buf->rows[win->cursor.y].chars.data,
                     buf->rows[win->cursor.y].chars.len);

    /* Fire hook before deletion */
    HookLineEvent event = {buf, win->cursor.y, buf->rows[win->cursor.y].chars.data,
                          buf->rows[win->cursor.y].chars.len};
    hook_fire_line(HOOK_LINE_DELETE, &event);

    if (!undo_is_applying()) {
        int y0 = win->cursor.y;
        SizedStr cap = sstr_new();
        sstr_append(&cap, buf->rows[y0].chars.data, buf->rows[y0].chars.len);
        sstr_append_char(&cap, '\n');
        undo_begin_group();
        undo_push_delete(y0, 0, cap.data, cap.len, y0, 0, y0, 0);
        sstr_free(&cap);
    }

    buf_row_del_in(buf, win->cursor.y);
    if (buf->num_rows == 0) {
        buf_row_insert_in(buf, 0, "", 0);
        win->cursor.y = 0;
        win->row_offset = 0;
    } else if (win->cursor.y >= buf->num_rows) {
        win->cursor.y = buf->num_rows - 1;
    }
    win->cursor.x = 0;
}
void buf_yank_line_in(Buffer *buf) {
    Window *win = window_cur();
    if (!PTR_VALID(buf) || !PTR_VALID(win)) return;
    if (!BOUNDS_CHECK(win->cursor.y, buf->num_rows)) return;

    sstr_free(&E.clipboard);
    E.clipboard = sstr_from(buf->rows[win->cursor.y].chars.data,
                            buf->rows[win->cursor.y].chars.len);
    /* Update registers: yank '0' and unnamed */
    regs_set_yank(buf->rows[win->cursor.y].chars.data,
                  buf->rows[win->cursor.y].chars.len);
}

void buf_paste_in(Buffer *buf) {
    Window *win = window_cur();
    if (!buf || !win) return;
    if (buf->readonly) {
        ed_set_status_message("Buffer is read-only");
        return;
    }
    if (E.clipboard.len == 0) return;

    int at = (win->cursor.y < buf->num_rows) ? (win->cursor.y + 1) : buf->num_rows;
    if (!undo_is_applying()) {
        undo_begin_group();
        undo_push_insert(at, 0, E.clipboard.data, E.clipboard.len,
                         win->cursor.y, win->cursor.x, at, 0);
    }
    if (win->cursor.y < buf->num_rows) {
        win->cursor.y++;
    }
    buf_row_insert_in(buf, win->cursor.y, E.clipboard.data, E.clipboard.len);
    win->cursor.x = 0;
}

/*** Search ***/

void buf_find_in(Buffer *buf) {
    if (!buf) return;
    if (E.search_query.len == 0) return;

    Window *win = window_cur();
    int start_y = win ? win->cursor.y : 0;
    int current = start_y;

    for (int i = 0; i < buf->num_rows; i++) {
        current = (start_y + i + 1) % buf->num_rows;
        Row *row = &buf->rows[current];
        char *match = strstr(row->render.data, E.search_query.data);
        if (match) {
            if (win) {
                win->cursor.y = current;
                win->cursor.x = buf_row_rx_to_cx(row, match - row->render.data);
            }
            Window *win = window_cur();
            if (win) win->row_offset = buf->num_rows;
            ed_set_status_message("Found at line %d", current + 1);
            return;
        }
    }

    ed_set_status_message("Not found: %s", E.search_query.data);
}

/* Reload current buffer's file from disk, discarding unsaved changes */
void buf_reload(Buffer *buf) {
    if (!buf || !buf->filename) {
        ed_set_status_message("reload: no file");
        return;
    }
    /* Clear existing rows */
    for (int i = 0; i < buf->num_rows; i++) {
        row_free(&buf->rows[i]);
    }
    free(buf->rows);
    buf->rows = NULL;
    buf->num_rows = 0;
    /* reset scroll will be handled by window */
    buf->cursor.x = 0;
    buf->cursor.y = 0;

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
        buf_row_insert_in(buf, buf->num_rows, line, (size_t)len);
    }
    free(line);
    fclose(fp);
    buf->dirty = 0;
    ed_set_status_message("reloaded: %s", buf->filename);
}
