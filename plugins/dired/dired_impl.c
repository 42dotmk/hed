#include "dired.h"
#include "hed.h"
#include <dirent.h>
#include <limits.h>
#include <stdint.h>
#include <sys/stat.h>

void buf_row_insert_in(Buffer *buf, int at, const char *s, size_t len);

/* Each rendered line is prefixed with "/XXXX/ " — 4 hex digits flanked by
 * slashes, then a space. The prefix gives every entry a stable opaque ID so
 * a save can distinguish renames from delete+create. */
#define DIRED_PREFIX_LEN 7
#define DIRED_TMP_PREFIX ".__dired_tmp_"

typedef struct {
    uint32_t id;
    char name[PATH_MAX]; /* basename, no trailing slash */
    int is_dir;
} DiredSnapshotEntry;

typedef DiredSnapshotEntry *DiredSnapshotVec;

typedef struct {
    Buffer *buf;
    char origin[PATH_MAX];
    char cwd[PATH_MAX];
    DiredSnapshotVec snapshot;
    uint32_t next_id;
} DiredState;

typedef DiredState *DiredStateVec;
static DiredStateVec dired_states = NULL;

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
    for (ptrdiff_t i = 0; i < arrlen(dired_states); i++) {
        if (dired_states[i].buf == buf) {
            if (out_idx) *out_idx = (size_t)i;
            return &dired_states[i];
        }
    }
    if (out_idx) *out_idx = (size_t)-1;
    return NULL;
}

static DiredState *dired_state_create(Buffer *buf, const char *dir) {
    if (!buf || !dir)
        return NULL;

    DiredState st = {.buf = buf};
    dired_copy(st.origin, sizeof(st.origin), dir);
    dired_copy(st.cwd, sizeof(st.cwd), dir);
    st.next_id = 1;
    arrput(dired_states, st);
    return &dired_states[arrlen(dired_states) - 1];
}

static void dired_state_free(DiredState *st) {
    if (!st)
        return;
    arrfree(st->snapshot);
    st->snapshot = NULL;
}

static void dired_state_remove(Buffer *buf) {
    size_t idx = (size_t)-1;
    DiredState *st = dired_state_find(buf, &idx);
    if (!st || idx == (size_t)-1)
        return;
    dired_state_free(st);
    arrdel(dired_states, idx);
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
    buf->cursor->x = 0;
    buf->cursor->y = 0;
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

static int dired_hex_digit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* Parse a rendered dired line. On success, fills *has_id, *id, name buffer
 * (without trailing slash), *is_dir. Returns:
 *   1 = parsed (may be a known-id line or a free-form new line)
 *   0 = empty (skip)
 *  -1 = invalid (e.g. embedded slash, "." or "..") */
static int dired_parse_line(const char *src, size_t srclen, int *has_id,
                            uint32_t *id, char *name, size_t name_sz,
                            int *is_dir) {
    *has_id = 0;
    *id = 0;
    *is_dir = 0;
    name[0] = '\0';

    /* Strip trailing whitespace */
    while (srclen > 0 &&
           (src[srclen - 1] == ' ' || src[srclen - 1] == '\t' ||
            src[srclen - 1] == '\r'))
        srclen--;

    /* Strip leading whitespace */
    size_t start = 0;
    while (start < srclen && (src[start] == ' ' || src[start] == '\t'))
        start++;

    if (start >= srclen)
        return 0;

    /* Try to parse "/XXXX/ " prefix */
    if (srclen - start >= DIRED_PREFIX_LEN && src[start] == '/' &&
        src[start + 5] == '/' && src[start + 6] == ' ') {
        int d0 = dired_hex_digit(src[start + 1]);
        int d1 = dired_hex_digit(src[start + 2]);
        int d2 = dired_hex_digit(src[start + 3]);
        int d3 = dired_hex_digit(src[start + 4]);
        if (d0 >= 0 && d1 >= 0 && d2 >= 0 && d3 >= 0) {
            *has_id = 1;
            *id = (uint32_t)((d0 << 12) | (d1 << 8) | (d2 << 4) | d3);
            start += DIRED_PREFIX_LEN;
        }
    }

    if (start >= srclen)
        return 0;

    size_t nlen = srclen - start;
    if (nlen + 1 > name_sz)
        nlen = name_sz - 1;
    memcpy(name, src + start, nlen);
    name[nlen] = '\0';

    /* Trailing slash → directory */
    while (nlen > 0 && name[nlen - 1] == '/') {
        *is_dir = 1;
        name[nlen - 1] = '\0';
        nlen--;
    }

    if (nlen == 0)
        return 0;

    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return -1;

    /* Reject any embedded slash — we only allow basenames */
    for (size_t i = 0; i < nlen; i++) {
        if (name[i] == '/')
            return -1;
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
        /* Hide internal temp-rename markers from prior aborted saves */
        if (strncmp(de->d_name, DIRED_TMP_PREFIX,
                    sizeof(DIRED_TMP_PREFIX) - 1) == 0)
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

    /* Reset snapshot for the new listing */
    arr_reset(st->snapshot);
    st->next_id = 1;

    for (size_t i = 0; i < count; i++) {
        uint32_t id = st->next_id++;
        DiredSnapshotEntry snap = {.id = id, .is_dir = entries[i].is_dir};
        dired_copy(snap.name, sizeof(snap.name), entries[i].name);
        arrput(st->snapshot, snap);

        char line[PATH_MAX];
        snprintf(line, sizeof(line), "/%04x/ %s%s", id, entries[i].name,
                 entries[i].is_dir ? "/" : "");
        buf_row_insert_in(buf, buf->num_rows, line, strlen(line));
    }
    free(entries);

    dired_copy(st->cwd, sizeof(st->cwd), target);
    buf->dirty = 0;
    Window *win = window_cur();
    if (win && win->buffer_index == (int)(buf - E.buffers)) {
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
        DiredState *st = dired_state_find(&E.buffers[existing], NULL);
        if (st)
            dired_list_dir(st, st->cwd);
        return;
    }

    int idx = -1;
    if (buf_new(resolved, &idx) != ED_OK) {
        ed_set_status_message("dired: failed to open buffer");
        return;
    }

    Buffer *buf = &E.buffers[idx];
    free(buf->title);
    buf->title = strdup("dired");
    free(buf->filetype);
    buf->filetype = strdup("dired");
    /* Editable: user adds/removes/renames lines, then :w to commit */
    buf->readonly = 0;
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

/* Extract the displayed name (after the optional ID prefix), with trailing
 * slash dropped. Returns 1 on success. */
static int dired_row_name(Buffer *buf, int row, char *out, size_t out_sz,
                          int *is_dir_out) {
    if (!buf || !out || out_sz == 0)
        return 0;
    if (row < 0 || row >= buf->num_rows) {
        out[0] = '\0';
        return 0;
    }
    Row *r = &buf->rows[row];
    int has_id = 0;
    uint32_t id = 0;
    int is_dir = 0;
    int rc = dired_parse_line(r->chars.data, r->chars.len, &has_id, &id, out,
                              out_sz, &is_dir);
    (void)id;
    (void)has_id;
    if (is_dir_out)
        *is_dir_out = is_dir;
    return rc == 1;
}

int dired_handle_enter(void) {
    DiredState *st = dired_state_from_current();
    Window *win = window_cur();
    if (!st || !win)
        return 0;
    if (win->cursor.y < 0 || win->cursor.y >= st->buf->num_rows)
        return 1;

    char name[PATH_MAX];
    int is_dir = 0;
    if (!dired_row_name(st->buf, win->cursor.y, name, sizeof(name), &is_dir))
        return 1;
    if (!name[0])
        return 1;

    char path[PATH_MAX];
    if (!dired_join(st->cwd, name, path, sizeof(path)))
        return 1;

    if (is_dir || path_is_dir(path)) {
        dired_list_dir(st, path);
    } else if (path_exists(path)) {
        buf_open_or_switch(path, true);
    } else {
        ed_set_status_message("dired: %s does not exist (save with :w first)",
                              name);
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

/* ============================================================
 *  Save: diff buffer rows vs snapshot, apply create/rename/delete
 * ============================================================ */

typedef enum {
    DIRED_OP_CREATE,
    DIRED_OP_RENAME,
    DIRED_OP_DELETE,
} DiredOpKind;

typedef struct {
    DiredOpKind kind;
    char src_name[PATH_MAX];  /* delete/rename source */
    char dst_name[PATH_MAX];  /* create/rename target */
    int is_dir;
    char tmp_name[PATH_MAX];  /* used by two-pass rename */
} DiredOp;

typedef DiredOp *DiredOpVec;

typedef struct {
    int has_id;
    uint32_t id;
    char name[PATH_MAX];
    int is_dir;
    int row;
} DiredCurrent;

typedef DiredCurrent *DiredCurrentVec;

static int dired_find_snapshot_by_id(DiredState *st, uint32_t id,
                                     size_t *out_idx) {
    for (ptrdiff_t i = 0; i < arrlen(st->snapshot); i++) {
        if (st->snapshot[i].id == id) {
            if (out_idx)
                *out_idx = (size_t)i;
            return 1;
        }
    }
    return 0;
}

/* Returns 0 on success, -1 on validation failure (status set, no ops applied) */
static int dired_collect_current(DiredState *st, DiredCurrentVec *out) {
    Buffer *buf = st->buf;
    for (int row = 0; row < buf->num_rows; row++) {
        Row *r = &buf->rows[row];
        DiredCurrent c = {.row = row};
        int rc = dired_parse_line(r->chars.data, r->chars.len, &c.has_id, &c.id,
                                  c.name, sizeof(c.name), &c.is_dir);
        if (rc == 0)
            continue;
        if (rc < 0) {
            ed_set_status_message(
                "dired: invalid name on line %d (no '/' or '.', '..')",
                row + 1);
            return -1;
        }
        arrput(*out, c);
    }

    /* Reject duplicate IDs (e.g. user copy-pasted a line) */
    for (ptrdiff_t i = 0; i < arrlen(*out); i++) {
        if (!(*out)[i].has_id)
            continue;
        for (ptrdiff_t j = i + 1; j < arrlen(*out); j++) {
            if (!(*out)[j].has_id)
                continue;
            if ((*out)[i].id == (*out)[j].id) {
                ed_set_status_message(
                    "dired: duplicate id /%04x/ on lines %d and %d — "
                    "remove the prefix from one of them",
                    (*out)[i].id, (*out)[i].row + 1,
                    (*out)[j].row + 1);
                return -1;
            }
        }
    }

    /* Reject duplicate final names */
    for (ptrdiff_t i = 0; i < arrlen(*out); i++) {
        for (ptrdiff_t j = i + 1; j < arrlen(*out); j++) {
            if (strcmp((*out)[i].name, (*out)[j].name) == 0) {
                ed_set_status_message(
                    "dired: duplicate name '%s' on lines %d and %d",
                    (*out)[i].name, (*out)[i].row + 1,
                    (*out)[j].row + 1);
                return -1;
            }
        }
    }
    return 0;
}

static int dired_build_plan(DiredState *st, DiredCurrentVec *current,
                            DiredOpVec *ops) {
    /* Mark each snapshot entry as matched/unmatched */
    char *matched = NULL;
    if (arrlen(st->snapshot) > 0) {
        matched = calloc(arrlen(st->snapshot), 1);
        if (!matched) {
            ed_set_status_message("dired: out of memory");
            return -1;
        }
    }

    /* Pass 1: classify each current row */
    for (ptrdiff_t i = 0; i < arrlen(*current); i++) {
        DiredCurrent *c = &(*current)[i];
        size_t snap_idx = (size_t)-1;
        if (c->has_id && dired_find_snapshot_by_id(st, c->id, &snap_idx)) {
            DiredSnapshotEntry *snap = &st->snapshot[snap_idx];
            matched[snap_idx] = 1;
            if (strcmp(snap->name, c->name) == 0 && snap->is_dir == c->is_dir) {
                /* Unchanged */
                continue;
            }
            if (snap->is_dir != c->is_dir) {
                ed_set_status_message(
                    "dired: cannot change file/dir type via rename: '%s'",
                    snap->name);
                free(matched);
                return -1;
            }
            DiredOp op = {.kind = DIRED_OP_RENAME, .is_dir = snap->is_dir};
            dired_copy(op.src_name, sizeof(op.src_name), snap->name);
            dired_copy(op.dst_name, sizeof(op.dst_name), c->name);
            arrput(*ops, op);
        } else {
            /* No id, or unknown id → treat as a new file/dir to create */
            DiredOp op = {.kind = DIRED_OP_CREATE, .is_dir = c->is_dir};
            dired_copy(op.dst_name, sizeof(op.dst_name), c->name);
            arrput(*ops, op);
        }
    }

    /* Pass 2: any unmatched snapshot entry was deleted */
    for (ptrdiff_t i = 0; i < arrlen(st->snapshot); i++) {
        if (matched[i])
            continue;
        DiredOp op = {.kind = DIRED_OP_DELETE,
                      .is_dir = st->snapshot[i].is_dir};
        dired_copy(op.src_name, sizeof(op.src_name),
                   st->snapshot[i].name);
        arrput(*ops, op);
    }

    free(matched);
    return 0;
}

/* Apply renames using a two-pass temp rename to handle cycles like A→B B→A.
 * Returns count of renames applied; sets *had_error on first failure. */
static int dired_apply_renames(DiredState *st, DiredOpVec *ops, int *had_error) {
    int applied = 0;

    /* Pass 1: source → temp */
    for (ptrdiff_t i = 0; i < arrlen(*ops); i++) {
        DiredOp *op = &(*ops)[i];
        if (op->kind != DIRED_OP_RENAME)
            continue;
        snprintf(op->tmp_name, sizeof(op->tmp_name), "%s%td",
                 DIRED_TMP_PREFIX, i);
        char src[PATH_MAX], tmp[PATH_MAX];
        if (!dired_join(st->cwd, op->src_name, src, sizeof(src)) ||
            !dired_join(st->cwd, op->tmp_name, tmp, sizeof(tmp))) {
            ed_set_status_message("dired: path too long: %s", op->src_name);
            *had_error = 1;
            op->tmp_name[0] = '\0';
            continue;
        }
        if (rename(src, tmp) != 0) {
            ed_set_status_message("dired: rename %s: %s", op->src_name,
                                  strerror(errno));
            *had_error = 1;
            op->tmp_name[0] = '\0';
        }
    }

    /* Pass 2: temp → destination */
    for (ptrdiff_t i = 0; i < arrlen(*ops); i++) {
        DiredOp *op = &(*ops)[i];
        if (op->kind != DIRED_OP_RENAME)
            continue;
        if (!op->tmp_name[0])
            continue;
        char tmp[PATH_MAX], dst[PATH_MAX];
        if (!dired_join(st->cwd, op->tmp_name, tmp, sizeof(tmp)) ||
            !dired_join(st->cwd, op->dst_name, dst, sizeof(dst))) {
            ed_set_status_message("dired: path too long: %s", op->dst_name);
            *had_error = 1;
            continue;
        }
        if (path_exists(dst)) {
            ed_set_status_message("dired: target exists: %s", op->dst_name);
            *had_error = 1;
            /* Roll the temp back to original name to leave fs consistent */
            char src[PATH_MAX];
            if (dired_join(st->cwd, op->src_name, src, sizeof(src)))
                rename(tmp, src);
            continue;
        }
        if (rename(tmp, dst) != 0) {
            ed_set_status_message("dired: rename %s -> %s: %s", op->src_name,
                                  op->dst_name, strerror(errno));
            *had_error = 1;
            continue;
        }
        applied++;
    }
    return applied;
}

static int dired_apply_deletes(DiredState *st, DiredOpVec *ops, int *had_error) {
    int applied = 0;
    for (ptrdiff_t i = 0; i < arrlen(*ops); i++) {
        DiredOp *op = &(*ops)[i];
        if (op->kind != DIRED_OP_DELETE)
            continue;
        char path[PATH_MAX];
        if (!dired_join(st->cwd, op->src_name, path, sizeof(path))) {
            ed_set_status_message("dired: path too long: %s", op->src_name);
            *had_error = 1;
            continue;
        }
        int rc;
        if (op->is_dir) {
            rc = rmdir(path);
            if (rc != 0 && (errno == ENOTEMPTY || errno == EEXIST)) {
                ed_set_status_message(
                    "dired: directory not empty: %s (delete contents first)",
                    op->src_name);
                *had_error = 1;
                continue;
            }
        } else {
            rc = unlink(path);
        }
        if (rc != 0) {
            ed_set_status_message("dired: delete %s: %s", op->src_name,
                                  strerror(errno));
            *had_error = 1;
            continue;
        }
        applied++;
    }
    return applied;
}

static int dired_apply_creates(DiredState *st, DiredOpVec *ops, int *had_error) {
    int applied = 0;
    for (ptrdiff_t i = 0; i < arrlen(*ops); i++) {
        DiredOp *op = &(*ops)[i];
        if (op->kind != DIRED_OP_CREATE)
            continue;
        char path[PATH_MAX];
        if (!dired_join(st->cwd, op->dst_name, path, sizeof(path))) {
            ed_set_status_message("dired: path too long: %s", op->dst_name);
            *had_error = 1;
            continue;
        }
        if (path_exists(path)) {
            ed_set_status_message("dired: already exists: %s", op->dst_name);
            *had_error = 1;
            continue;
        }
        if (op->is_dir) {
            if (mkdir(path, 0755) != 0) {
                ed_set_status_message("dired: mkdir %s: %s", op->dst_name,
                                      strerror(errno));
                *had_error = 1;
                continue;
            }
        } else {
            int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
            if (fd < 0) {
                ed_set_status_message("dired: create %s: %s", op->dst_name,
                                      strerror(errno));
                *had_error = 1;
                continue;
            }
            close(fd);
        }
        applied++;
    }
    return applied;
}

/* Pending-confirm state. At most one dired save can be awaiting confirmation
 * at a time; the modal blocks input so re-entry shouldn't happen, but we
 * guard against it anyway. */
static struct {
    int active;
    int dired_buf_idx;    /* dired buffer that owns the pending plan */
    int confirm_buf_idx;  /* scratch buffer rendered into the modal */
    Window *modal;
    DiredOpVec ops;
} dired_pending;

static void dired_render_plan(Buffer *buf, const char *cwd,
                              const DiredOpVec *ops, int *out_max_w) {
    int n_create = 0, n_rename = 0, n_delete = 0;
    for (ptrdiff_t i = 0; i < arrlen(*ops); i++) {
        switch ((*ops)[i].kind) {
        case DIRED_OP_CREATE: n_create++; break;
        case DIRED_OP_RENAME: n_rename++; break;
        case DIRED_OP_DELETE: n_delete++; break;
        }
    }

    char line[PATH_MAX * 2];
    int max_w = 0;

#define APPEND(...)                                                            \
    do {                                                                       \
        int __n = snprintf(line, sizeof(line), __VA_ARGS__);                   \
        if (__n < 0) __n = 0;                                                  \
        if (__n > max_w) max_w = __n;                                          \
        buf_row_insert_in(buf, buf->num_rows, line, (size_t)strnlen(line, sizeof(line))); \
    } while (0)

    APPEND("dired: confirm changes");
    APPEND("in: %s", cwd);
    APPEND("%s", "");

    if (n_create > 0) {
        APPEND("CREATE (%d):", n_create);
        for (ptrdiff_t i = 0; i < arrlen(*ops); i++) {
            const DiredOp *op = &(*ops)[i];
            if (op->kind != DIRED_OP_CREATE) continue;
            APPEND("  + %s%s", op->dst_name, op->is_dir ? "/" : "");
        }
        APPEND("%s", "");
    }
    if (n_rename > 0) {
        APPEND("RENAME (%d):", n_rename);
        for (ptrdiff_t i = 0; i < arrlen(*ops); i++) {
            const DiredOp *op = &(*ops)[i];
            if (op->kind != DIRED_OP_RENAME) continue;
            APPEND("  ~ %s -> %s", op->src_name, op->dst_name);
        }
        APPEND("%s", "");
    }
    if (n_delete > 0) {
        APPEND("DELETE (%d):", n_delete);
        for (ptrdiff_t i = 0; i < arrlen(*ops); i++) {
            const DiredOp *op = &(*ops)[i];
            if (op->kind != DIRED_OP_DELETE) continue;
            APPEND("  - %s%s", op->src_name, op->is_dir ? "/" : "");
        }
        APPEND("%s", "");
    }

    APPEND("y: apply   n / q / <Esc>: cancel   j / k: scroll");

#undef APPEND

    if (out_max_w)
        *out_max_w = max_w;
}

static int dired_show_confirm_modal(DiredState *st, DiredOpVec ops) {
    int dired_idx = (int)(st->buf - E.buffers);

    int buf_idx = -1;
    if (buf_new(NULL, &buf_idx) != ED_OK) {
        ed_set_status_message("dired: confirm modal: buf_new failed");
        arrfree(ops);
        return 0;
    }
    Buffer *cb = &E.buffers[buf_idx];
    free(cb->filename); cb->filename = NULL;
    free(cb->title); cb->title = strdup("dired confirm");
    free(cb->filetype); cb->filetype = strdup("dired_confirm");

    int max_w = 0;
    dired_render_plan(cb, st->cwd, &ops, &max_w);
    cb->dirty = 0;
    cb->readonly = 1;

    int width = max_w + 2;
    if (width < 32) width = 32;
    if (width > E.screen_cols - 6) width = E.screen_cols - 6;
    int height = cb->num_rows;
    if (height < 5) height = 5;
    if (height > E.screen_rows - 6) height = E.screen_rows - 6;

    Window *modal = winmodal_create(-1, -1, width, height);
    if (!modal) {
        cb->dirty = 0;
        buf_close(buf_idx);
        arrfree(ops);
        ed_set_status_message("dired: confirm modal: winmodal_create failed");
        return 0;
    }
    modal->buffer_index = buf_idx;
    winmodal_show(modal);

    dired_pending.active = 1;
    dired_pending.dired_buf_idx = dired_idx;
    dired_pending.confirm_buf_idx = buf_idx;
    dired_pending.modal = modal;
    dired_pending.ops = ops; /* ownership transferred */

    ed_set_status_message("dired: y to apply, n/q/<Esc> to cancel");
    return 1;
}

static void dired_dismiss_pending(int do_apply) {
    if (!dired_pending.active)
        return;

    /* Snapshot pending fields and clear global state up front so that any
     * reentrant call (e.g. via buf_close hooks) is a no-op. */
    Window *modal = dired_pending.modal;
    int confirm_idx = dired_pending.confirm_buf_idx;
    int dired_idx = dired_pending.dired_buf_idx;
    DiredOpVec ops = dired_pending.ops;
    memset(&dired_pending, 0, sizeof(dired_pending));
    dired_pending.dired_buf_idx = -1;
    dired_pending.confirm_buf_idx = -1;

    if (modal) {
        winmodal_hide(modal);
        winmodal_destroy(modal);
    }

    /* Closing the confirm buffer shifts higher indices down by one. */
    if (confirm_idx >= 0 && confirm_idx < (int)arrlen(E.buffers)) {
        E.buffers[confirm_idx].dirty = 0;
        buf_close(confirm_idx);
        if (dired_idx > confirm_idx)
            dired_idx--;
    }

    Buffer *dbuf = NULL;
    if (dired_idx >= 0 && dired_idx < (int)arrlen(E.buffers)) {
        Buffer *cand = &E.buffers[dired_idx];
        if (cand->filetype && strcmp(cand->filetype, "dired") == 0)
            dbuf = cand;
    }
    DiredState *st = dbuf ? dired_state_find(dbuf, NULL) : NULL;

    if (do_apply && st) {
        int had_error = 0;
        int n_renamed = dired_apply_renames(st, &ops, &had_error);
        int n_deleted = dired_apply_deletes(st, &ops, &had_error);
        int n_created = dired_apply_creates(st, &ops, &had_error);
        dired_list_dir(st, st->cwd);
        if (had_error) {
            log_msg("dired: applied with errors — created=%d renamed=%d "
                    "deleted=%d", n_created, n_renamed, n_deleted);
        } else {
            ed_set_status_message("dired: created %d, renamed %d, deleted %d",
                                  n_created, n_renamed, n_deleted);
        }
    } else {
        ed_set_status_message("dired: cancelled");
    }

    arrfree(ops);
}

int dired_handle_save(Buffer *buf) {
    if (!buf || !buf->filetype || strcmp(buf->filetype, "dired") != 0)
        return 0;

    if (dired_pending.active) {
        ed_set_status_message(
            "dired: confirm or cancel pending changes first");
        return 1;
    }

    DiredState *st = dired_state_find(buf, NULL);
    if (!st) {
        ed_set_status_message("dired: no state for buffer");
        return 1;
    }

    DiredCurrentVec current = NULL;
    DiredOpVec ops = NULL;

    if (dired_collect_current(st, &current) != 0) {
        arrfree(current);
        arrfree(ops);
        return 1;
    }

    if (dired_build_plan(st, &current, &ops) != 0) {
        arrfree(current);
        arrfree(ops);
        return 1;
    }

    arrfree(current);

    if (arrlen(ops) == 0) {
        arrfree(ops);
        buf->dirty = 0;
        ed_set_status_message("dired: no changes");
        return 1;
    }

    /* Ownership of ops moves into the modal; success or failure of show
     * frees it appropriately. */
    dired_show_confirm_modal(st, ops);
    return 1;
}

static void dired_keypress_hook(HookKeyEvent *event) {
    if (!dired_pending.active || !event)
        return;
    Window *modal = winmodal_current();
    if (!modal || modal != dired_pending.modal)
        return;

    int key = event->key;
    switch (key) {
    case 'y':
    case 'Y':
        dired_dismiss_pending(1);
        break;
    case 'n':
    case 'N':
    case 'q':
    case '\x1b':
        dired_dismiss_pending(0);
        break;
    case 'j':
    case KEY_ARROW_DOWN: {
        int idx = modal->buffer_index;
        if (idx >= 0 && idx < (int)arrlen(E.buffers)) {
            Buffer *cb = &E.buffers[idx];
            int max_off = cb->num_rows - modal->height;
            if (max_off < 0) max_off = 0;
            if (modal->row_offset < max_off) modal->row_offset++;
        }
        break;
    }
    case 'k':
    case KEY_ARROW_UP:
        if (modal->row_offset > 0) modal->row_offset--;
        break;
    default:
        break;
    }
    event->consumed = 1;
}

static void dired_on_buffer_close(HookBufferEvent *event) {
    if (!event || !event->buf)
        return;
    if (!event->buf->filetype || strcmp(event->buf->filetype, "dired") != 0)
        return;
    /* If the dired buffer being closed is the one with a pending plan,
     * cancel without applying — keeps state consistent. */
    if (dired_pending.active) {
        int idx = (int)(event->buf - E.buffers);
        if (idx == dired_pending.dired_buf_idx)
            dired_dismiss_pending(0);
    }
    dired_state_remove(event->buf);
}

void dired_hooks_init(void) {
    dired_pending.dired_buf_idx = -1;
    dired_pending.confirm_buf_idx = -1;
    int modes[] = {MODE_NORMAL, MODE_INSERT, MODE_COMMAND, MODE_VISUAL,
                   MODE_VISUAL_BLOCK};
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        hook_register_buffer(HOOK_BUFFER_CLOSE, modes[i], "dired",
                             dired_on_buffer_close);
    }
    hook_register_key(HOOK_KEYPRESS, dired_keypress_hook);
}
