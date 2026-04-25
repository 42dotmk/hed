#include "dired.h"
#include "hed.h"
#include "lib/file_helpers.h"
#include "vector.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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

VEC_DEFINE(DiredSnapshotVec, DiredSnapshotEntry);

typedef struct {
    Buffer *buf;
    char origin[PATH_MAX];
    char cwd[PATH_MAX];
    DiredSnapshotVec snapshot;
    uint32_t next_id;
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
    st.next_id = 1;
    vec_push_typed(&dired_states, DiredState, st);

    if (dired_states.len == old_len)
        return NULL;

    return &dired_states.data[dired_states.len - 1];
}

static void dired_state_free(DiredState *st) {
    if (!st)
        return;
    free(st->snapshot.data);
    st->snapshot.data = NULL;
    st->snapshot.len = 0;
    st->snapshot.cap = 0;
}

static void dired_state_remove(Buffer *buf) {
    size_t idx = (size_t)-1;
    DiredState *st = dired_state_find(buf, &idx);
    if (!st || idx == (size_t)-1)
        return;
    dired_state_free(st);
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
    vec_clear(&st->snapshot);
    st->next_id = 1;

    for (size_t i = 0; i < count; i++) {
        uint32_t id = st->next_id++;
        DiredSnapshotEntry snap = {.id = id, .is_dir = entries[i].is_dir};
        dired_copy(snap.name, sizeof(snap.name), entries[i].name);
        vec_push_typed(&st->snapshot, DiredSnapshotEntry, snap);

        char line[PATH_MAX];
        snprintf(line, sizeof(line), "/%04x/ %s%s", id, entries[i].name,
                 entries[i].is_dir ? "/" : "");
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

VEC_DEFINE(DiredOpVec, DiredOp);

typedef struct {
    int has_id;
    uint32_t id;
    char name[PATH_MAX];
    int is_dir;
    int row;
} DiredCurrent;

VEC_DEFINE(DiredCurrentVec, DiredCurrent);

static int dired_find_snapshot_by_id(DiredState *st, uint32_t id,
                                     size_t *out_idx) {
    for (size_t i = 0; i < st->snapshot.len; i++) {
        if (st->snapshot.data[i].id == id) {
            if (out_idx)
                *out_idx = i;
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
        vec_push_typed(out, DiredCurrent, c);
    }

    /* Reject duplicate IDs (e.g. user copy-pasted a line) */
    for (size_t i = 0; i < out->len; i++) {
        if (!out->data[i].has_id)
            continue;
        for (size_t j = i + 1; j < out->len; j++) {
            if (!out->data[j].has_id)
                continue;
            if (out->data[i].id == out->data[j].id) {
                ed_set_status_message(
                    "dired: duplicate id /%04x/ on lines %d and %d — "
                    "remove the prefix from one of them",
                    out->data[i].id, out->data[i].row + 1,
                    out->data[j].row + 1);
                return -1;
            }
        }
    }

    /* Reject duplicate final names */
    for (size_t i = 0; i < out->len; i++) {
        for (size_t j = i + 1; j < out->len; j++) {
            if (strcmp(out->data[i].name, out->data[j].name) == 0) {
                ed_set_status_message(
                    "dired: duplicate name '%s' on lines %d and %d",
                    out->data[i].name, out->data[i].row + 1,
                    out->data[j].row + 1);
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
    if (st->snapshot.len > 0) {
        matched = calloc(st->snapshot.len, 1);
        if (!matched) {
            ed_set_status_message("dired: out of memory");
            return -1;
        }
    }

    /* Pass 1: classify each current row */
    for (size_t i = 0; i < current->len; i++) {
        DiredCurrent *c = &current->data[i];
        size_t snap_idx = (size_t)-1;
        if (c->has_id && dired_find_snapshot_by_id(st, c->id, &snap_idx)) {
            DiredSnapshotEntry *snap = &st->snapshot.data[snap_idx];
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
            vec_push_typed(ops, DiredOp, op);
        } else {
            /* No id, or unknown id → treat as a new file/dir to create */
            DiredOp op = {.kind = DIRED_OP_CREATE, .is_dir = c->is_dir};
            dired_copy(op.dst_name, sizeof(op.dst_name), c->name);
            vec_push_typed(ops, DiredOp, op);
        }
    }

    /* Pass 2: any unmatched snapshot entry was deleted */
    for (size_t i = 0; i < st->snapshot.len; i++) {
        if (matched[i])
            continue;
        DiredOp op = {.kind = DIRED_OP_DELETE,
                      .is_dir = st->snapshot.data[i].is_dir};
        dired_copy(op.src_name, sizeof(op.src_name),
                   st->snapshot.data[i].name);
        vec_push_typed(ops, DiredOp, op);
    }

    free(matched);
    return 0;
}

/* Apply renames using a two-pass temp rename to handle cycles like A→B B→A.
 * Returns count of renames applied; sets *had_error on first failure. */
static int dired_apply_renames(DiredState *st, DiredOpVec *ops, int *had_error) {
    int applied = 0;

    /* Pass 1: source → temp */
    for (size_t i = 0; i < ops->len; i++) {
        DiredOp *op = &ops->data[i];
        if (op->kind != DIRED_OP_RENAME)
            continue;
        snprintf(op->tmp_name, sizeof(op->tmp_name), "%s%zu",
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
    for (size_t i = 0; i < ops->len; i++) {
        DiredOp *op = &ops->data[i];
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
    for (size_t i = 0; i < ops->len; i++) {
        DiredOp *op = &ops->data[i];
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
    for (size_t i = 0; i < ops->len; i++) {
        DiredOp *op = &ops->data[i];
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

int dired_handle_save(Buffer *buf) {
    if (!buf || !buf->filetype || strcmp(buf->filetype, "dired") != 0)
        return 0;

    DiredState *st = dired_state_find(buf, NULL);
    if (!st) {
        ed_set_status_message("dired: no state for buffer");
        return 1;
    }

    DiredCurrentVec current = {0};
    DiredOpVec ops = {0};

    if (dired_collect_current(st, &current) != 0)
        goto done;

    if (dired_build_plan(st, &current, &ops) != 0)
        goto done;

    if (ops.len == 0) {
        buf->dirty = 0;
        ed_set_status_message("dired: no changes");
        goto done;
    }

    int had_error = 0;
    int n_renamed = dired_apply_renames(st, &ops, &had_error);
    int n_deleted = dired_apply_deletes(st, &ops, &had_error);
    int n_created = dired_apply_creates(st, &ops, &had_error);

    /* Re-read the directory; status from list_dir would clobber our summary,
     * so capture it after. */
    dired_list_dir(st, st->cwd);

    if (had_error) {
        /* The most recent specific error is already in the status line from
         * the failing op; append a summary so the user knows what did succeed. */
        log_msg("dired: applied with errors — created=%d renamed=%d deleted=%d",
                n_created, n_renamed, n_deleted);
    } else {
        ed_set_status_message("dired: created %d, renamed %d, deleted %d",
                              n_created, n_renamed, n_deleted);
    }

done:
    free(current.data);
    free(ops.data);
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
