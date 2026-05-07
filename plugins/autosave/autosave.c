/* autosave plugin: writes a copy of every dirty buffer into the
 * editor's per-cwd cache dir on a debounced idle timer (default 3 s
 * after the last keystroke) and on every transition out of INSERT
 * mode. The autosave is deleted on the next manual `:w`.
 *
 * Layout. For a buffer whose filename is `src/foo.c` and an editor
 * cwd of `/mnt/storage/probe/hed`, the autosave lives at:
 *
 *     ~/.cache/hed/%mnt%storage%probe%hed/autosave/src/foo.c
 *
 * which mirrors the cwd's structure under autosave/ — easy to
 * inspect manually, easy to clean up (`rm -rf` the cwd's cache dir).
 *
 * Recovery. On HOOK_BUFFER_OPEN, if an autosave is newer than the
 * on-disk file, the user is asked (via the ui/ask helper) whether
 * to restore it. Yes => content swapped in, buffer marked dirty
 * (saving requires explicit `:w`). No => stale autosave is deleted.
 *
 * Skips. Buffers without a filename, read-only buffers, plugin-
 * owned scratch buffers (titles starting with `[`), and files
 * larger than AUTOSAVE_MAX_BYTES.
 *
 * Config. `:autosave on|off|toggle|status|restore`. Default on. */

#include "hed.h"
#include "select_loop.h"
#include "prompt.h"
#include "autosave.h"

#include <fcntl.h>
#include <sys/stat.h>

/* Defined in src/buf/buffer.c, not exposed in buffer.h. */
void buf_row_insert_in(Buffer *buf, int at, const char *s, size_t len);

#define AUTOSAVE_IDLE_MS    3000
#define AUTOSAVE_MAX_BYTES  (10 * 1024 * 1024)

/* ---------- module state ---------- */

static int g_enabled = 1;

/* ---------- skip rules ---------- */

static int autosave_skip_buf(const Buffer *buf) {
    if (!buf || !buf->filename || !*buf->filename) return 1;
    if (buf->readonly) return 1;
    /* Plugin scratch buffers ([copilot], [scratch], [claude], ...). */
    if (buf->title && buf->title[0] == '[') return 1;
    return 0;
}

/* ---------- path computation ---------- */

/* Build `~/.cache/hed/<encoded-cwd>/autosave/<rel>`. Caller frees. */
static char *autosave_path_for(const char *filename) {
    char base[PATH_MAX];
    if (!path_cache_file_for_cwd("autosave", base, sizeof(base))) return NULL;

    /* Strip any leading slashes so absolute paths nest cleanly under
     * the autosave dir without producing `<base>//abs/path`. */
    while (*filename == '/') filename++;
    if (!*filename) return NULL;

    size_t n   = strlen(base) + 1 + strlen(filename) + 1;
    char  *out = malloc(n);
    if (!out) return NULL;
    snprintf(out, n, "%s/%s", base, filename);
    return out;
}

/* mkdir -p on the parent directory of `path`. Returns 0 on success. */
static int autosave_mkdir_parent(const char *path) {
    const char *slash = strrchr(path, '/');
    if (!slash) return 0;
    size_t n   = (size_t)(slash - path);
    char  *dir = malloc(n + 1);
    if (!dir) return -1;
    memcpy(dir, path, n);
    dir[n] = '\0';
    int ok = path_mkdir_p(dir);
    free(dir);
    return ok ? 0 : -1;
}

/* ---------- buffer text builder ---------- */

static char *autosave_build_text(const Buffer *buf, size_t *out_len) {
    size_t total = 0;
    for (int i = 0; i < buf->num_rows; i++)
        total += buf->rows[i].chars.len + 1;
    char *s = malloc(total + 1);
    if (!s) return NULL;
    size_t off = 0;
    for (int i = 0; i < buf->num_rows; i++) {
        memcpy(s + off, buf->rows[i].chars.data, buf->rows[i].chars.len);
        off += buf->rows[i].chars.len;
        s[off++] = '\n';
    }
    s[off] = '\0';
    if (out_len) *out_len = off;
    return s;
}

/* ---------- atomic write ---------- */

static int autosave_atomic_write(const char *path, const char *data, size_t n) {
    char tmp[PATH_MAX];
    int  m = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (m <= 0 || (size_t)m >= sizeof(tmp)) return -1;

    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, data + off, n - off);
        if (w < 0) { close(fd); unlink(tmp); return -1; }
        off += (size_t)w;
    }
    fsync(fd);
    close(fd);
    if (rename(tmp, path) != 0) { unlink(tmp); return -1; }
    return 0;
}

/* ---------- write the autosave for one buffer ---------- */

static void autosave_write_buf(Buffer *buf) {
    if (!g_enabled) return;
    if (autosave_skip_buf(buf)) return;
    if (!buf->dirty) return;

    size_t n;
    char *text = autosave_build_text(buf, &n);
    if (!text) return;
    if (n > AUTOSAVE_MAX_BYTES) {
        log_msg("autosave: skip %s (%zu bytes > %d cap)",
                buf->filename, n, AUTOSAVE_MAX_BYTES);
        free(text);
        return;
    }

    char *path = autosave_path_for(buf->filename);
    if (!path) { free(text); return; }

    if (autosave_mkdir_parent(path) != 0) {
        log_msg("autosave: mkdir failed for %s", path);
        free(text); free(path);
        return;
    }
    if (autosave_atomic_write(path, text, n) != 0) {
        log_msg("autosave: write failed for %s: %s", path, strerror(errno));
    } else {
        log_msg("autosave: wrote %zu bytes to %s", n, path);
    }
    free(text); free(path);
}

/* ---------- timer fire: write every dirty buffer ---------- */

static void autosave_fire(void *ud) {
    (void)ud;
    if (!g_enabled) return;
    for (ptrdiff_t i = 0; i < arrlen(E.buffers); i++) {
        autosave_write_buf(&E.buffers[i]);
    }
}

static void autosave_schedule(void) {
    if (!g_enabled) return;
    ed_loop_timer_after("autosave:idle", AUTOSAVE_IDLE_MS,
                        autosave_fire, NULL);
}

/* ---------- hooks ---------- */

static void on_char_insert(const HookCharEvent *e) { (void)e; autosave_schedule(); }
static void on_char_delete(const HookCharEvent *e) { (void)e; autosave_schedule(); }

static void on_mode_change(const HookModeEvent *e) {
    if (!g_enabled || !e) return;
    if (e->old_mode == MODE_INSERT) {
        ed_loop_timer_cancel("autosave:idle");
        autosave_fire(NULL);
    }
}

static void on_buffer_save(HookBufferEvent *e) {
    if (!e || !e->buf || !e->buf->filename) return;
    if (autosave_skip_buf(e->buf)) return;
    char *path = autosave_path_for(e->buf->filename);
    if (!path) return;
    if (unlink(path) == 0) log_msg("autosave: removed %s after save", path);
    free(path);
}

/* ---------- recovery on open ---------- */

typedef struct {
    int   buf_idx;
    char *autosave_path;
    char *display_name;     /* heap copy of buf->filename for status msgs */
} RestorePending;

/* Replace `buf`'s rows with the contents of `path`, mark dirty.
 * Mirrors the read loop in buf_reload(). */
static int autosave_load_into(Buffer *buf, const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    /* Drop existing rows. */
    for (int i = 0; i < buf->num_rows; i++) row_free(&buf->rows[i]);
    free(buf->rows);
    buf->rows     = NULL;
    buf->num_rows = 0;
    if (buf->cursor) { buf->cursor->x = 0; buf->cursor->y = 0; }

    /* Drop undo so the user can't accidentally undo back into the
     * pre-restore state — that would be confusing. */
    undo_state_free(&buf->undo);
    undo_state_init(&buf->undo);

    /* Drop vtext marks pinned to old line indices. */
    vtext_clear_all(buf);

    char  *line = NULL;
    size_t cap  = 0;
    ssize_t len;
    while ((len = getline(&line, &cap, fp)) != -1) {
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) len--;
        buf_row_insert_in(buf, buf->num_rows, line, (size_t)len);
    }
    free(line);
    fclose(fp);

    buf->dirty = 1;
    return 0;
}

static void on_restore_choice(const char *answer, void *ud) {
    RestorePending *rp = ud;
    if (!rp) return;
    int yes = answer && (answer[0] == 'y' || answer[0] == 'Y');

    Buffer *buf = (rp->buf_idx >= 0 && rp->buf_idx < (int)arrlen(E.buffers))
                  ? &E.buffers[rp->buf_idx] : NULL;

    if (yes && buf) {
        if (autosave_load_into(buf, rp->autosave_path) == 0) {
            ed_set_status_message(
                "autosave: restored %s (buffer modified, :w to commit)",
                rp->display_name);
        } else {
            ed_set_status_message(
                "autosave: restore failed for %s",
                rp->display_name);
        }
    } else {
        if (unlink(rp->autosave_path) == 0)
            ed_set_status_message("autosave: discarded for %s",
                                  rp->display_name);
    }
    free(rp->autosave_path);
    free(rp->display_name);
    free(rp);
}

/* Returns 1 if a usable autosave exists for `buf` (newer than its
 * on-disk file). 0 otherwise. */
static int autosave_exists_and_fresh(const Buffer *buf, char **out_path) {
    char *path = autosave_path_for(buf->filename);
    if (!path) return 0;

    struct stat st_a;
    if (stat(path, &st_a) != 0) { free(path); return 0; }

    struct stat st_o;
    if (stat(buf->filename, &st_o) == 0 && st_a.st_mtime <= st_o.st_mtime) {
        /* On-disk file is at least as new — the autosave is stale.
         * Remove it silently. */
        unlink(path);
        free(path);
        return 0;
    }

    if (out_path) *out_path = path; else free(path);
    return 1;
}

/* Open the recovery prompt for `buf`. Caller has already verified the
 * autosave exists and is fresh. */
static void autosave_prompt_restore(Buffer *buf, char *autosave_path) {
    RestorePending *rp = calloc(1, sizeof(*rp));
    if (!rp) { free(autosave_path); return; }
    rp->buf_idx       = (int)(buf - E.buffers);
    rp->autosave_path = autosave_path;
    rp->display_name  = strdup(buf->filename);

    char q[512];
    snprintf(q, sizeof(q),
             "autosave found for %s — restore? (y/n)",
             buf->filename);
    ask(q, "y", on_restore_choice, rp);
}

static void on_buffer_open(HookBufferEvent *e) {
    if (!g_enabled || !e || !e->buf) return;
    if (autosave_skip_buf(e->buf)) return;

    char *path = NULL;
    if (!autosave_exists_and_fresh(e->buf, &path)) return;

    /* If a prompt is already up (e.g. multiple files opened from the
     * cli), ask() will refuse — fall back to a status message and let
     * the user run :autosave restore manually. */
    if (prompt_active()) {
        ed_set_status_message(
            "autosave found for %s — :autosave restore to load",
            e->buf->filename);
        free(path);
        return;
    }
    autosave_prompt_restore(e->buf, path);
}

/* ---------- :autosave subcommands ---------- */

static void cmd_autosave(const char *args) {
    while (args && *args == ' ') args++;
    if (!args || !*args || strcmp(args, "status") == 0) {
        ed_set_status_message("autosave: %s, idle=%dms, cap=%dMB",
                              g_enabled ? "on" : "off",
                              AUTOSAVE_IDLE_MS,
                              AUTOSAVE_MAX_BYTES / (1024 * 1024));
        return;
    }
    if (strcmp(args, "on") == 0)     { g_enabled = 1; ed_set_status_message("autosave: on");     return; }
    if (strcmp(args, "off") == 0)    { g_enabled = 0; ed_loop_timer_cancel("autosave:idle");
                                       ed_set_status_message("autosave: off");                    return; }
    if (strcmp(args, "toggle") == 0) { g_enabled = !g_enabled;
                                       if (!g_enabled) ed_loop_timer_cancel("autosave:idle");
                                       ed_set_status_message("autosave: %s",
                                                             g_enabled ? "on" : "off");
                                       return; }
    if (strcmp(args, "restore") == 0) {
        Buffer *buf = buf_cur();
        if (!buf) { ed_set_status_message("autosave: no buffer"); return; }
        if (autosave_skip_buf(buf)) {
            ed_set_status_message("autosave: nothing to restore (no filename)");
            return;
        }
        char *path = NULL;
        if (!autosave_exists_and_fresh(buf, &path)) {
            ed_set_status_message("autosave: no fresh autosave for %s",
                                  buf->filename);
            return;
        }
        autosave_prompt_restore(buf, path);
        return;
    }
    if (strcmp(args, "now") == 0) {
        autosave_fire(NULL);
        ed_set_status_message("autosave: flushed");
        return;
    }
    ed_set_status_message("autosave: unknown subcommand '%s'", args);
}

/* ---------- plugin lifecycle ---------- */

static int autosave_init(void) {
    cmd("autosave", cmd_autosave,
        "autosave on|off|toggle|status|restore|now");

    hook_register_char  (HOOK_CHAR_INSERT,  MODE_INSERT, "*", on_char_insert);
    hook_register_char  (HOOK_CHAR_DELETE,  MODE_INSERT, "*", on_char_delete);
    hook_register_mode  (HOOK_MODE_CHANGE,  on_mode_change);
    hook_register_buffer(HOOK_BUFFER_OPEN,  -1, "*", on_buffer_open);
    hook_register_buffer(HOOK_BUFFER_SAVE,  -1, "*", on_buffer_save);

    return 0;
}

static void autosave_deinit(void) {
    ed_loop_timer_cancel("autosave:idle");
}

const Plugin plugin_autosave = {
    .name   = "autosave",
    .desc   = "idle/timer autosave to per-cwd cache dir, with recovery prompt",
    .init   = autosave_init,
    .deinit = autosave_deinit,
};
