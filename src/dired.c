#include "dired.h"
#include "hed.h"
#include "lib/file_helpers.h"
#include "vector.h"
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

void buf_row_insert_in(Buffer *buf, int at, const char *s, size_t len);

typedef struct {
    Buffer *buf;
    char origin[PATH_MAX];
    char cwd[PATH_MAX];
} DiredState;

VEC_DEFINE(DiredStateVec, DiredState);
static DiredStateVec dired_states;

static void dired_copy(char *dst, size_t dstsz, const char *src) {
    if (!dst || dstsz == 0)
        return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dstsz, "%s", src);
}

static DiredState *dired_state_find(Buffer *buf, size_t *out_idx) {
    if (!buf)
        return NULL;
    return vec_find(&dired_states, DiredState, (__elem->buf == buf), out_idx);
}

static DiredState *dired_state_create(Buffer *buf, const char *dir) {
    if (!buf || !dir)
        return NULL;

    size_t old_len = dired_states.len;
    DiredState st = {.buf = buf};
    dired_copy(st.origin, sizeof(st.origin), dir);
    dired_copy(st.cwd, sizeof(st.cwd), dir);
    vec_push_typed(&dired_states, DiredState, st);

    /* Check if push succeeded */
    if (dired_states.len == old_len)
        return NULL;

    return &dired_states.data[dired_states.len - 1];
}

static void dired_state_remove(Buffer *buf) {
    size_t idx = (size_t)-1;
    if (!dired_state_find(buf, &idx) || idx == (size_t)-1)
        return;
    vec_remove(&dired_states, DiredState, idx);
}

static void dired_clear_buffer(Buffer *buf) {
    if (!buf)
        return;
    for (int i = 0; i < buf->num_rows; i++) {
        row_free(&buf->rows[i]);
    }
    free(buf->rows);
    buf->rows = NULL;
    buf->num_rows = 0;
    buf->cursor.x = 0;
    buf->cursor.y = 0;
}

typedef struct {
    char name[PATH_MAX];
    int is_dir;
} DiredEntry;

static int dired_entry_cmp(const void *a, const void *b) {
    const DiredEntry *ea = (const DiredEntry *)a;
    const DiredEntry *eb = (const DiredEntry *)b;
    return strcmp(ea->name, eb->name);
}

static void dired_trim_slash(char *s) {
    if (!s)
        return;
    size_t len = strlen(s);
    while (len && s[len - 1] == '/') {
        s[len - 1] = '\0';
        len--;
    }
}

static int dired_join(const char *dir, const char *name, char *out,
                      size_t out_sz) {
    char clean[PATH_MAX];
    dired_copy(clean, sizeof(clean), name);
    dired_trim_slash(clean);
    if (!path_join_dir(out, out_sz, dir, clean)) {
        if (out_sz)
            out[0] = '\0';
        return 0;
    }
    return 1;
}

static int dired_list_dir(DiredState *st, const char *dir) {
    if (!st || !dir || !*dir)
        return 0;
    char resolved[PATH_MAX];
    const char *target = dir;
    if (realpath(dir, resolved))
        target = resolved;

    DIR *dp = opendir(target);
    if (!dp) {
        ed_set_status_message("dired: %s: %s", target, strerror(errno));
        return 0;
    }

    DiredEntry *entries = NULL;
    size_t count = 0, cap = 0;
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        if (count == cap) {
            cap = cap ? cap * 2 : 32;
            void *mem = realloc(entries, cap * sizeof(DiredEntry));
            if (!mem) {
                closedir(dp);
                free(entries);
                ed_set_status_message("dired: out of memory");
                return 0;
            }
            entries = mem;
        }
        dired_copy(entries[count].name, sizeof(entries[count].name),
                   de->d_name);
        char full[PATH_MAX];
        entries[count].is_dir =
            dired_join(target, de->d_name, full, sizeof(full)) &&
            path_is_dir(full);
        count++;
    }
    closedir(dp);

    if (count > 1)
        qsort(entries, count, sizeof(DiredEntry), dired_entry_cmp);

    Buffer *buf = st->buf;
    dired_clear_buffer(buf);
    for (size_t i = 0; i < count; i++) {
        char line[PATH_MAX];
        dired_copy(line, sizeof(line), entries[i].name);
        if (entries[i].is_dir)
            strncat(line, "/", sizeof(line) - strlen(line) - 1);
        buf_row_insert_in(buf, buf->num_rows, line, strlen(line));
    }
    free(entries);

    dired_copy(st->cwd, sizeof(st->cwd), target);
    buf->dirty = 0;
    Window *win = window_cur();
    if (win && win->buffer_index == (int)(buf - E.buffers.data)) {
        win->cursor.x = 0;
        win->cursor.y = 0;
        win->row_offset = 0;
    }
    ed_set_status_message("dired: %s", st->cwd);
    return 1;
}

static DiredState *dired_state_from_current(void) {
    Buffer *buf = buf_cur();
    if (!buf || !buf->filetype || strcmp(buf->filetype, "dired") != 0)
        return NULL;
    return dired_state_find(buf, NULL);
}

void dired_open(const char *path) {
    if (!path || !*path)
        return;
    char resolved[PATH_MAX];
    if (!realpath(path, resolved))
        dired_copy(resolved, sizeof(resolved), path);
    if (!path_is_dir(resolved)) {
        ed_set_status_message("dired: not a directory: %s", resolved);
        return;
    }

    int existing = buf_find_by_filename(resolved);
    if (existing >= 0) {
        buf_switch(existing);
        DiredState *st = dired_state_find(&E.buffers.data[existing], NULL);
        if (st)
            dired_list_dir(st, st->cwd);
        return;
    }

    int idx = -1;
    if (buf_new(resolved, &idx) != ED_OK) {
        ed_set_status_message("dired: failed to open buffer");
        return;
    }

    Buffer *buf = &E.buffers.data[idx];
    free(buf->title);
    buf->title = strdup("dired");
    free(buf->filetype);
    buf->filetype = strdup("dired");
    buf->readonly = 1;
    buf->dirty = 0;

    DiredState *st = dired_state_create(buf, resolved);
    if (!st) {
        ed_set_status_message("dired: out of memory");
        return;
    }
    if (!dired_list_dir(st, resolved))
        return;

    Window *win = window_cur();
    if (win)
        win_attach_buf(win, buf);
    E.current_buffer = idx;
}

static int dired_row_text(Buffer *buf, int row, char *out, size_t out_sz) {
    if (!buf || !out || out_sz == 0)
        return 0;
    if (row < 0 || row >= buf->num_rows) {
        out[0] = '\0';
        return 0;
    }
    Row *r = &buf->rows[row];
    size_t len = r->chars.len;
    if (len >= out_sz)
        len = out_sz - 1;
    memcpy(out, r->chars.data, len);
    out[len] = '\0';
    return 1;
}

int dired_handle_enter(void) {
    DiredState *st = dired_state_from_current();
    Window *win = window_cur();
    if (!st || !win)
        return 0;
    if (win->cursor.y < 0 || win->cursor.y >= st->buf->num_rows)
        return 1;

    char name[PATH_MAX];
    if (!dired_row_text(st->buf, win->cursor.y, name, sizeof(name)))
        return 1;
    int is_dir = name[0] && name[strlen(name) - 1] == '/';
    dired_trim_slash(name);

    char path[PATH_MAX];
    if (!dired_join(st->cwd, name, path, sizeof(path)))
        return 1;

    if (is_dir || path_is_dir(path)) {
        dired_list_dir(st, path);
    } else {
        buf_open_or_switch(path, true);
    }
    return 1;
}

int dired_handle_parent(void) {
    DiredState *st = dired_state_from_current();
    if (!st)
        return 0;
    char parent[PATH_MAX];
    path_dirname_buf(st->cwd, parent, sizeof(parent));
    if (!parent[0])
        return 1;
    dired_list_dir(st, parent);
    return 1;
}

int dired_handle_home(void) {
    DiredState *st = dired_state_from_current();
    if (!st)
        return 0;
    if (!st->origin[0])
        return 1;
    dired_list_dir(st, st->origin);
    return 1;
}

int dired_handle_chdir(void) {
    DiredState *st = dired_state_from_current();
    if (!st)
        return 0;
    if (!st->cwd[0])
        return 1;

    if (chdir(st->cwd) == 0) {
        if (getcwd(E.cwd, sizeof(E.cwd))) {
            ed_set_status_message("cd: %s", E.cwd);
        } else {
            E.cwd[0] = '\0';
            ed_set_status_message("cd: ok");
        }
    } else {
        ed_set_status_message("cd: %s", strerror(errno));
    }
    return 1;
}

static void dired_on_buffer_close(const HookBufferEvent *event) {
    if (!event || !event->buf)
        return;
    if (!event->buf->filetype || strcmp(event->buf->filetype, "dired") != 0)
        return;
    dired_state_remove(event->buf);
}

void dired_hooks_init(void) {
    int modes[] = {MODE_NORMAL, MODE_INSERT, MODE_COMMAND, MODE_VISUAL,
                   MODE_VISUAL_BLOCK};
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        hook_register_buffer(HOOK_BUFFER_CLOSE, modes[i], "dired",
                             dired_on_buffer_close);
    }
}
