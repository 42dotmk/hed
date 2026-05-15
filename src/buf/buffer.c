#include "buf/buf_helpers.h"
#include "buf/buffer.h"
#include "registers.h"
#include "editor.h"
#include "hooks.h"
#include "terminal.h"
#include "lib/log.h"
#include "lib/strutil.h"
#include "stb_ds.h"
#include "utils/recent_files.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lib/safe_string.h"
#include "fold_methods/fold_methods.h"
#include "lib/file_helpers.h"
#include <assert.h>
#include <regex.h>

/* Weak refs to the treesitter plugin (skipped when not linked). */
extern int ts_is_enabled(void)               __attribute__((weak));
extern int ts_buffer_autoload(Buffer *buf)   __attribute__((weak));
extern void ts_buffer_reparse(Buffer *buf)   __attribute__((weak));
extern void ts_buffer_free(Buffer *buf)      __attribute__((weak));

/* Internal low-level row helpers (not part of public API) */
void buf_row_insert_in(Buffer *buf, int at, const char *s, size_t len);
void buf_row_del_in(Buffer *buf, int at);
void buf_row_insert_char_in(Buffer *buf, Row *row, int at, int c);
void buf_row_append_in(Buffer *buf, Row *row, const SizedStr *str);
void buf_row_del_char_in(Buffer *buf, Row *row, int at);

Buffer *buf_cur(void) {
    if (arrlen(E.buffers) == 0)
        return NULL;
    return &E.buffers[E.current_buffer];
}

int buf_find_by_filename(const char *filename) {
    if (!filename)
        return -1;
    for (int i = 0; i < (int)arrlen(E.buffers); i++) {
        if (E.buffers[i].filename &&
            strcmp(E.buffers[i].filename, filename) == 0) {
            return i;
        }
    }
    return -1;
}

/* Initialize a Buffer struct in-place with default values */
static void buf_init(Buffer *buf) {
    if (!buf)
        return;
    buf->rows = NULL;
    buf->num_rows = 0;
    buf->all_cursors = NULL;
    buf->cursor = NULL;
    /* Always start with one active cursor at (0,0). */
    Cursor *c0 = calloc(1, sizeof(Cursor));
    if (c0) {
        arrput(buf->all_cursors, c0);
        buf->cursor = c0;
    }
    buf->filename = NULL;
    buf->title = strdup("[No Name]");
    if (!buf->title)
        buf->title = NULL; /* Handle OOM gracefully */
    buf->filetype = NULL;
    buf->dirty = 0;
    buf->readonly = 0; /* Default: not read-only */
    buf->ts_internal = NULL;
    fold_list_init(&buf->folds);
    buf->fold_method = NULL; /* Filetype default applied on BUFFER_OPEN */
    undo_state_init(&buf->undo);
    vtext_init(buf);
}

/* Create a new buffer and return EdError status */
EdError buf_new(const char *filename, int *out_idx) {
    if (!PTR_VALID(out_idx))
        return ED_ERR_INVALID_ARG;
    *out_idx = -1;

    /* Append a default-initialized slot, then fill it in place. On any
     * error we arrpop to roll back the slot. */
    Buffer slot = {0};
    arrput(E.buffers, slot);
    int idx = (int)arrlen(E.buffers) - 1;
    Buffer *buf = &E.buffers[idx];
    buf_init(buf);

    if (filename && *filename) {
        free(buf->title);
        buf->title = strdup(filename);
        if (!buf->title) {
            (void)arrpop(E.buffers);
            return ED_ERR_NOMEM;
        }

        buf->filename = strdup(filename);
        if (!buf->filename) {
            free(buf->title);
            buf->title = NULL;
            (void)arrpop(E.buffers);
            return ED_ERR_NOMEM;
        }
    }

    buf->filetype = path_detect_filetype(filename);
    if (!buf->filetype) {
        free(buf->title);
        free(buf->filename);
        (void)arrpop(E.buffers);
        return ED_ERR_NOMEM;
    }

    *out_idx = idx;
    return ED_OK;
}

/* Opens a file and returns EdError status */
EdError buf_open_file(const char *filename, Buffer **out) {
    if (!PTR_VALID(out))
        return ED_ERR_INVALID_ARG;
    *out = NULL;
    if (!PTR_VALID(filename))
        return ED_ERR_INVALID_ARG;
    int idx;
    EdError err = buf_new(filename, &idx);
    if (err != ED_OK)
        return err;
    Buffer *buf = &E.buffers[idx];

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        /* New file - this is OK, not an error */
        ed_set_status_message("New file: %s", filename);
        *out = buf;
        return ED_OK;
    }

    // Add to jump list

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 &&
               (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
            linelen--;
        buf_row_insert_in(buf, buf->num_rows, line, linelen);
    }
    free(line);
    fclose(fp);
    buf->dirty = 0;

    recent_files_add(&E.recent_files, filename);
    HookBufferEvent event = {.buf = buf, .filename = filename};
    hook_fire_buffer(HOOK_BUFFER_OPEN, &event);

    ed_set_status_message("Loaded: %s", filename);
    if (ts_is_enabled && ts_is_enabled()) {
        if (ts_buffer_autoload) ts_buffer_autoload(buf);
        if (ts_buffer_reparse)  ts_buffer_reparse(buf);
    }
    *out = buf;

    return ED_OK;
}

void buf_open_or_switch(const char *filename, bool add_to_jumplist) {
    if (!filename || !*filename) {
        ed_set_status_message("No filename provided");
        return;
    }
    /* Allow plugins (e.g., dired) to intercept the open. */
    {
        HookBufferEvent ev = {0};
        ev.filename = filename;
        hook_fire_buffer(HOOK_BUFFER_OPEN_PRE, &ev);
        if (ev.consumed)
            return;
    }

    /* Record current position BEFORE switching so <C-o> returns here */
    if (add_to_jumplist) {
        Buffer *cur = buf_cur();
        Window *win = window_cur();
        if (cur && cur->filename) {
            int cx = win ? win->cursor.x : cur->cursor->x;
            int cy = win ? win->cursor.y : cur->cursor->y;
            jump_list_add(&E.jump_list, cur->filename, cx, cy);
        }
    }

    /* Check if buffer already exists */
    int found = buf_find_by_filename(filename);
    if (found >= 0) {
        EdError err = buf_switch(found);
        if (err == ED_OK) {
            ed_set_status_message("Switched to: %s", filename);
        } else {
            ed_set_status_message("Failed to switch: %s", ed_error_string(err));
        }
    } else {
        Buffer *nb = NULL;
        EdError err = buf_open_file(filename, &nb);
        if (err == ED_OK || err == ED_ERR_FILE_NOT_FOUND) {
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
    if (!BOUNDS_CHECK(index, arrlen(E.buffers))) {
        return ED_ERR_INVALID_INDEX;
    }

    /* Record current position before switching */
    Window *win = window_cur();
    Buffer *buf = buf_cur();
    E.current_buffer = index;
    if (win){
        win->buffer_index = index;
		win->cursor.x = buf->cursor->x;
		win->cursor.y = buf->cursor->y;
	}

    /* Fire hook */
    HookBufferEvent event = {.buf = buf, .filename = buf->filename};
    hook_fire_buffer(HOOK_BUFFER_SWITCH, &event);

    return ED_OK;
}

void buf_next(void) {
    if (arrlen(E.buffers) <= 1)
        return;

    /* Record current position before switching */
    Window *win = window_cur();

    E.current_buffer = (E.current_buffer + 1) % arrlen(E.buffers);
    if (win)
        win->buffer_index = E.current_buffer;
    Buffer *buf = buf_cur();

    /* Fire hook */
    HookBufferEvent event = {.buf = buf, .filename = buf->filename};
    hook_fire_buffer(HOOK_BUFFER_SWITCH, &event);

    if (win) {
        jump_list_add(&E.jump_list, buf->filename, win->cursor.x,
                      win->cursor.y);
    }
    ed_set_status_message("Buffer %d: %s", E.current_buffer + 1, buf->title);
}

void buf_prev(void) {
    if (arrlen(E.buffers) <= 1)
        return;

    /* Record current position before switching */
    Window *win = window_cur();

    E.current_buffer = (E.current_buffer - 1 + arrlen(E.buffers)) % arrlen(E.buffers);
    if (win)
        win->buffer_index = E.current_buffer;
    Buffer *buf = buf_cur();

    if (win) {
        jump_list_add(&E.jump_list, buf->filename, win->cursor.x,
                      win->cursor.y);
    }
    /* Fire hook */
    HookBufferEvent event = {.buf = buf, .filename = buf->filename};
    hook_fire_buffer(HOOK_BUFFER_SWITCH, &event);

    ed_set_status_message("Buffer %d: %s", E.current_buffer + 1, buf->title);
}

/* Close a buffer by index and return EdError status */
EdError buf_close(int index) {
    if (!BOUNDS_CHECK(index, arrlen(E.buffers))) {
        return ED_ERR_INVALID_INDEX;
    }

    Buffer *buf = &E.buffers[index];

    if (buf->dirty) {
        return ED_ERR_BUFFER_DIRTY;
    }

    bool was_current = (index == E.current_buffer);
    char *closed_filename = buf->filename ? strdup(buf->filename) : NULL;

    /* Fire hook before closing */
    HookBufferEvent event = {.buf = buf, .filename = buf->filename};
    hook_fire_buffer(HOOK_BUFFER_CLOSE, &event);

    /* Free buffer resources */
    /* Tree-sitter cleanup (no-op if plugin not linked). */
    if (ts_buffer_free) ts_buffer_free(buf);
    for (int i = 0; i < buf->num_rows; i++) {
        row_free(&buf->rows[i]);
    }
    free(buf->rows);
    free(buf->filename);
    free(buf->title);
    free(buf->filetype);
    free(buf->fold_method);
    for (ptrdiff_t i = 0; i < arrlen(buf->all_cursors); i++)
        free(buf->all_cursors[i]);
    arrfree(buf->all_cursors);
    buf->all_cursors = NULL;
    buf->cursor = NULL;
    fold_list_free(&buf->folds);
    undo_state_free(&buf->undo);
    vtext_free(buf);

    arrdel(E.buffers, index);

    if (arrlen(E.buffers) == 0) {
        /* Create an empty buffer */
        int idx;
        EdError err = buf_new(NULL, &idx);
        if (err == ED_OK) {
            E.current_buffer = 0;
        }
        /* If buffer creation fails, editor will be in an invalid state, but
         * better than crashing */
    } else {
        if (E.current_buffer > index) {
            /* Closed buffer was before current — keep current buffer focused. */
            E.current_buffer--;
        } else if (was_current) {
            /* Walk jump list from newest to oldest, skipping the just-closed
             * file, and switch to the first entry that still has an open
             * buffer. Falls through to the index-clamp below if nothing
             * matches. */
            int target = -1;
            for (ptrdiff_t i = arrlen(E.jump_list.entries) - 1; i >= 0; i--) {
                const char *fp = E.jump_list.entries[i].filepath;
                if (!fp) continue;
                if (closed_filename && strcmp(fp, closed_filename) == 0)
                    continue;
                int found = buf_find_by_filename(fp);
                if (found >= 0) {
                    target = found;
                    break;
                }
            }
            if (target >= 0) {
                E.current_buffer = target;
            } else if (E.current_buffer >= (int)arrlen(E.buffers)) {
                E.current_buffer = (int)arrlen(E.buffers) - 1;
            }
        } else if (E.current_buffer >= (int)arrlen(E.buffers)) {
            E.current_buffer = (int)arrlen(E.buffers) - 1;
        }

        /* Keep the focused window's buffer_index in sync. */
        Window *win = window_cur();
        if (win) win->buffer_index = E.current_buffer;

        /* Restore window cursor from the new buffer's cursor. */
        Buffer *new_buf = buf_cur();
        if (was_current && win && new_buf && new_buf->cursor) {
            win->cursor.x = new_buf->cursor->x;
            win->cursor.y = new_buf->cursor->y;
            HookBufferEvent ev = {.buf = new_buf, .filename = new_buf->filename};
            hook_fire_buffer(HOOK_BUFFER_SWITCH, &ev);
        }
    }

    free(closed_filename);
    return ED_OK;
}

/*** Multi-cursor list management ***/

Cursor *buf_cursor_add(Buffer *buf, int y, int x) {
    if (!buf) return NULL;
    Cursor *c = calloc(1, sizeof(Cursor));
    if (!c) return NULL;
    c->x = x;
    c->y = y;
    arrput(buf->all_cursors, c);
    return c;
}

int buf_cursor_remove(Buffer *buf, Cursor *c) {
    if (!buf || !c) return 0;
    if (c == buf->cursor) return 0;
    for (ptrdiff_t i = 0; i < arrlen(buf->all_cursors); i++) {
        if (buf->all_cursors[i] == c) {
            free(c);
            arrdel(buf->all_cursors, i);
            return 1;
        }
    }
    return 0;
}

void buf_cursor_clear_extras(Buffer *buf) {
    if (!buf || !buf->cursor) return;
    Cursor *keep = buf->cursor;
    for (ptrdiff_t i = 0; i < arrlen(buf->all_cursors); i++) {
        if (buf->all_cursors[i] != keep)
            free(buf->all_cursors[i]);
    }
    buf->all_cursors[0] = keep;
    arrsetlen(buf->all_cursors, 1);
}

int buf_cursor_set_active(Buffer *buf, Cursor *c) {
    if (!buf || !c) return 0;
    for (ptrdiff_t i = 0; i < arrlen(buf->all_cursors); i++) {
        if (buf->all_cursors[i] == c) {
            buf->cursor = c;
            return 1;
        }
    }
    return 0;
}

int buf_cursor_count(const Buffer *buf) {
    return buf ? (int)arrlen(buf->all_cursors) : 0;
}

void buf_cursor_sync_from_window(Buffer *buf) {
    Window *win = window_cur();
    if (!buf || !win || !buf->cursor) return;
    /* Only sync when the focused window shows this buffer. */
    if (win->buffer_index < 0 || win->buffer_index >= (int)arrlen(E.buffers))
        return;
    if (&E.buffers[win->buffer_index] != buf) return;
    buf->cursor->x = win->cursor.x;
    buf->cursor->y = win->cursor.y;
}

/*** Auto-shift helpers — applied to non-active cursors after edits.
 * The active cursor (buf->cursor) is updated separately via
 * buf_cursor_sync_from_window(). ***/

static void cursors_after_insert_char(Buffer *buf, int iy, int ix) {
    if (!buf) return;
    for (ptrdiff_t i = 0; i < arrlen(buf->all_cursors); i++) {
        Cursor *c = buf->all_cursors[i];
        if (c == buf->cursor) continue;
        if (c->y == iy && c->x >= ix) c->x++;
    }
}

static void cursors_after_delete_char(Buffer *buf, int iy, int ix) {
    if (!buf) return;
    for (ptrdiff_t i = 0; i < arrlen(buf->all_cursors); i++) {
        Cursor *c = buf->all_cursors[i];
        if (c == buf->cursor) continue;
        if (c->y == iy && c->x > ix) c->x--;
    }
}

static void cursors_after_insert_newline(Buffer *buf, int iy, int ix) {
    if (!buf) return;
    for (ptrdiff_t i = 0; i < arrlen(buf->all_cursors); i++) {
        Cursor *c = buf->all_cursors[i];
        if (c == buf->cursor) continue;
        if (c->y > iy) {
            c->y++;
        } else if (c->y == iy && c->x >= ix) {
            c->y++;
            c->x -= ix;
        }
    }
}

static void cursors_after_join_lines(Buffer *buf, int iy, int join_at) {
    if (!buf) return;
    for (ptrdiff_t i = 0; i < arrlen(buf->all_cursors); i++) {
        Cursor *c = buf->all_cursors[i];
        if (c == buf->cursor) continue;
        if (c->y == iy) {
            c->y--;
            c->x += join_at;
        } else if (c->y > iy) {
            c->y--;
        }
    }
}

static void cursors_after_delete_line(Buffer *buf, int iy) {
    if (!buf) return;
    for (ptrdiff_t i = 0; i < arrlen(buf->all_cursors); i++) {
        Cursor *c = buf->all_cursors[i];
        if (c == buf->cursor) continue;
        if (c->y > iy) {
            c->y--;
        } else if (c->y == iy) {
            /* Row gone; cursor lands on what's now at iy (or clamps). */
            if (c->y >= buf->num_rows) c->y = buf->num_rows > 0 ? buf->num_rows - 1 : 0;
            c->x = 0;
        }
    }
}

/*** Row operations ***/

/* Insert a row into a specific buffer (no window/state changes) */
void buf_row_insert_in(Buffer *buf, int at, const char *s, size_t len) {
    if (!buf)
        return;
    if (at < 0 || at > buf->num_rows)
        return;

    undo_record_insert(buf, at, s, len);

    Row *new_rows = realloc(buf->rows, sizeof(Row) * (buf->num_rows + 1));
    if (!new_rows) {
        ed_set_status_message("Out of memory");
        return;
    }
    buf->rows = new_rows;
    memmove(&buf->rows[at + 1], &buf->rows[at],
            sizeof(Row) * (buf->num_rows - at));

    buf->rows[at].chars = sstr_from(s, len);
    buf->rows[at].render = sstr_new();
    buf->rows[at].fold_start = false;
    buf->rows[at].fold_end = false;
    buf_row_update(&buf->rows[at]);

    buf->num_rows++;
    buf->dirty++;

    /* Fire hook */
    HookLineEvent event = {buf, at, s, len};
    hook_fire_line(HOOK_LINE_INSERT, &event);
}

void buf_row_del_in(Buffer *buf, int at) {
    if (!PTR_VALID(buf))
        return;
    if (!BOUNDS_CHECK(at, buf->num_rows))
        return;
    undo_record_delete(buf, at, buf->rows[at].chars.data,
                       buf->rows[at].chars.len);
    row_free(&buf->rows[at]);
    memmove(&buf->rows[at], &buf->rows[at + 1],
            sizeof(Row) * (buf->num_rows - at - 1));
    buf->num_rows--;
    buf->dirty++;
}

void buf_row_insert_char_in(Buffer *buf, Row *row, int at, int c) {
    if (!buf || !row)
        return;
    undo_record_replace(buf, (int)(row - buf->rows));
    sstr_insert_char(&row->chars, at, c);
    buf_row_update(row);
    buf->dirty++;
}

/* Legacy wrapper removed; use buf_row_insert_char_in */

void buf_row_append_in(Buffer *buf, Row *row, const SizedStr *str) {
    if (!buf || !row || !str)
        return;
    undo_record_replace(buf, (int)(row - buf->rows));
    sstr_append(&row->chars, str->data, str->len);
    buf_row_update(row);
    buf->dirty++;
}

void buf_row_del_char_in(Buffer *buf, Row *row, int at) {
    if (!buf || !row)
        return;
    if (at < 0 || at >= (int)row->chars.len)
        return;
    undo_record_replace(buf, (int)(row - buf->rows));
    sstr_delete_char(&row->chars, at);
    buf_row_update(row);
    buf->dirty++;
}

void buf_insert_char_in(Buffer *buf, int c) {
    Window *win = window_cur();
    if (!buf || !win)
        return;
    if (buf->readonly) {
        ed_set_status_message("Buffer is read-only");
        return;
    }
    if (win->cursor.y == buf->num_rows) {
        buf_row_insert_in(buf, buf->num_rows, "", 0);
    }
    int y0 = win->cursor.y;
    int x0 = win->cursor.x;
    buf_row_insert_char_in(buf, &buf->rows[y0], x0, c);
    win->cursor.x = x0 + 1;
    buf_cursor_sync_from_window(buf);
    cursors_after_insert_char(buf, y0, x0);
}

void buf_insert_newline_in(Buffer *buf) {
    Window *win = window_cur();
    if (!buf || !win)
        return;
    if (buf->readonly) {
        ed_set_status_message("Buffer is read-only");
        return;
    }
    int y0 = win->cursor.y;
    int x0 = win->cursor.x;
    if (x0 == 0) {
        buf_row_insert_in(buf, win->cursor.y, "", 0);
    } else {
        Row *row = &buf->rows[y0];
        const char *rest = row->chars.data + x0;
        size_t rest_len = row->chars.len - x0;
        /* Capture original row before split: row gets truncated to [0, x0)
         * and the rest moves into a new row. */
        undo_record_replace(buf, y0);
        buf_row_insert_in(buf, y0 + 1, rest, rest_len);

        row = &buf->rows[y0];
        row->chars.len = x0;
        row->chars.data[row->chars.len] = '\0';
        buf_row_update(row);
    }
    win->cursor.y = y0 + 1;
    win->cursor.x = 0;

    /* Guard: if cursor landed past the last row (e.g. empty buffer),
     * insert the missing row so cursor always points to a valid line. */
    if (win->cursor.y >= buf->num_rows)
        buf_row_insert_in(buf, win->cursor.y, "", 0);
    buf_cursor_sync_from_window(buf);
    cursors_after_insert_newline(buf, y0, x0);
}

void buf_del_char_in(Buffer *buf) {
    Window *win = window_cur();
    if (!buf || !win)
        return;
    if (buf->readonly) {
        ed_set_status_message("Buffer is read-only");
        return;
    }
    if (win->cursor.y == buf->num_rows)
        return;
    if (win->cursor.x == 0 && win->cursor.y == 0)
        return;

    int y = win->cursor.y;
    int x = win->cursor.x;
    Row *row = &buf->rows[y];
    if (x > 0) {
        int deleted_char =
            (x - 1 < (int)row->chars.len) ? row->chars.data[x - 1] : 0;
        buf_row_del_char_in(buf, row, x - 1);

        /* Fire hook */
        HookCharEvent event = {buf, y, x - 1, deleted_char};
        hook_fire_char(HOOK_CHAR_DELETE, &event);

        win->cursor.x = x - 1;
        buf_cursor_sync_from_window(buf);
        cursors_after_delete_char(buf, y, x - 1);
    } else {
        int prev_len = buf->rows[y - 1].chars.len;
        win->cursor.x = prev_len;
        buf_row_append_in(buf, &buf->rows[y - 1], &row->chars);
        buf_row_del_in(buf, y);
        win->cursor.y = y - 1;
        buf_cursor_sync_from_window(buf);
        cursors_after_join_lines(buf, y, prev_len);
    }
}

void buf_delete_line_in(Buffer *buf) {
    Window *win = window_cur();
    if (!buf || !win)
        return;
    if (buf->readonly) {
        ed_set_status_message("Buffer is read-only");
        return;
    }
    if (!BOUNDS_CHECK(win->cursor.y, buf->num_rows))
        return;

    /* Update registers: numbered delete and unnamed */
    regs_push_delete(buf->rows[win->cursor.y].chars.data,
                     buf->rows[win->cursor.y].chars.len);

    /* Fire hook before deletion */
    HookLineEvent event = {buf, win->cursor.y,
                           buf->rows[win->cursor.y].chars.data,
                           buf->rows[win->cursor.y].chars.len};
    hook_fire_line(HOOK_LINE_DELETE, &event);

    int deleted_y = win->cursor.y;
    buf_row_del_in(buf, win->cursor.y);
    if (buf->num_rows == 0) {
        buf_row_insert_in(buf, 0, "", 0);
        win->cursor.y = 0;
        win->row_offset = 0;
    } else if (win->cursor.y >= buf->num_rows) {
        win->cursor.y = buf->num_rows - 1;
    }
    win->cursor.x = 0;
    buf_cursor_sync_from_window(buf);
    cursors_after_delete_line(buf, deleted_y);
}
void buf_yank_line_in(Buffer *buf) {
    WIN(win)
    TextSelection sel;
    if (!textobj_line(buf, win->cursor.y, win->cursor.x, &sel))
        return;
    yank_selection(&sel);
}

/*** Search ***/

void buf_find_in(Buffer *buf) {
    if (!buf)
        return;
    if (E.search_query.len == 0)
        return;

    Window *win = window_cur();
    int start_y = win ? win->cursor.y : 0;
    int current = start_y;

    int use_regex = E.search_is_regex;
    regex_t regex;
    int regex_ready = 0;
    if (use_regex) {
        int rc = regcomp(&regex, E.search_query.data, REG_EXTENDED);
        if (rc != 0) {
            char errbuf[128];
            regerror(rc, &regex, errbuf, sizeof(errbuf));
            ed_set_status_message("Regex error: %s", errbuf);
            return;
        }
        regex_ready = 1;
    }

    for (int i = 0; i < buf->num_rows; i++) {
        current = (start_y + i + 1) % buf->num_rows;
        Row *row = &buf->rows[current];
        const char *render = row->render.data;
        const char *match = NULL;
        int rx = 0;

        if (regex_ready) {
            regmatch_t m;
            if (regexec(&regex, render, 1, &m, 0) == 0 && m.rm_so >= 0) {
                match = render + m.rm_so;
                rx = (int)m.rm_so;
            }
        } else {
            const char *lit = strstr(render, E.search_query.data);
            if (lit) {
                match = lit;
                rx = (int)(lit - render);
            }
        }

        if (match) {
            if (win) {
                win->cursor.y = current;
                win->cursor.x = buf_row_rx_to_cx(row, rx);
            }
            Window *cur = window_cur();
            if (cur)
                cur->row_offset = buf->num_rows;
            if (regex_ready)
                regfree(&regex);
            ed_set_status_message("Found%s at line %d",
                                  use_regex ? " (regex)" : "", current + 1);
            return;
        }
    }

    if (regex_ready)
        regfree(&regex);
    ed_set_status_message("Not found%s: %s", use_regex ? " (regex)" : "",
                          E.search_query.data);
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
    buf->cursor->x = 0;
    buf->cursor->y = 0;

    /* Clear folds */
    fold_list_free(&buf->folds);
    fold_list_init(&buf->folds);

    /* Drop undo history — rows are about to be replaced wholesale. */
    undo_state_free(&buf->undo);
    undo_state_init(&buf->undo);

    /* Drop virtual-text marks — they pin to line indices that the
     * about-to-be-replaced content may not have. */
    vtext_clear_all(buf);

    /* Detect filetype (update) */
    free(buf->filetype);
    buf->filetype = path_detect_filetype(buf->filename);

    FILE *fp = fopen(buf->filename, "r");
    if (!fp) {
        ed_set_status_message("reload: cannot open %s", buf->filename);
        buf->dirty = 0;
        return;
    }
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;
    while ((len = getline(&line, &cap, fp)) != -1) {
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            len--;
        buf_row_insert_in(buf, buf->num_rows, line, (size_t)len);
    }
    free(line);
    fclose(fp);
    buf->dirty = 0;
    ed_set_status_message("reloaded: %s", buf->filename);
}
