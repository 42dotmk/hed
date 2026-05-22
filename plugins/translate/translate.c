/* translate plugin: translate the buffer (or visual selection) into a
 * new vsplit buffer using translate-shell (`trans`).
 *
 * The translation runs asynchronously: we fork+exec `trans`, pipe its
 * stdout back, and register the read end with the editor's select loop
 * (see src/select_loop.h). The editor stays responsive while trans
 * works; when EOF arrives the callback assembles the result buffer.
 *
 * Only one job runs at a time — a second :translate while one is in
 * flight is rejected with a status message.
 *
 * Usage:
 *   :translate                  → default target, auto source
 *   :translate fr               → French target
 *   :translate fr en            → French target, English source
 *   In visual mode the active selection is translated instead of the
 *   whole buffer.
 */

#include "hed.h"
#include "translate.h"
#include "select_loop.h"
#include "utils/yank.h"
#include "ui/window.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* buf_row_insert_in is global but not exposed in any header — match the
 * forward-decl trick used by mail/yank/undo. */
void buf_row_insert_in(Buffer *buf, int at, const char *s, size_t len);

typedef struct TrJob {
    pid_t pid;
    int   from_fd;        /* child stdout read end (non-blocking) */
    char  in_path[64];    /* temp input file, unlink on finish */

    /* Output accumulator (grown with realloc). */
    char  *out;
    size_t out_len;
    size_t out_cap;

    /* Context captured at invocation time so the result is correct even
     * if the user has since switched buffers. */
    char *src_filetype;   /* strdup'd, transferred to dst->filetype */
    char *src_base;       /* strdup'd basename for the title */
    int   from_sel;
    char  target[16];
    char  source[16];
} TrJob;

static char    default_target[16] = "en";
static int     seq                = 0;
static TrJob  *job_active         = NULL;

void translate_set_default_target(const char *lang) {
    if (lang && *lang)
        snprintf(default_target, sizeof(default_target), "%s", lang);
}

static void job_free(TrJob *j) {
    if (!j) return;
    free(j->out);
    free(j->src_filetype);
    free(j->src_base);
    free(j);
}

/* ----- helpers shared with the sync version ------------------------- */

static char *buf_to_text(Buffer *buf, size_t *out_len) {
    size_t total = 0;
    for (int i = 0; i < buf->num_rows; i++)
        total += buf->rows[i].chars.len + 1;
    if (total == 0) total = 1;
    char *out = malloc(total + 1);
    if (!out) return NULL;
    size_t off = 0;
    for (int i = 0; i < buf->num_rows; i++) {
        SizedStr *s = &buf->rows[i].chars;
        if (s->len) { memcpy(out + off, s->data, s->len); off += s->len; }
        if (i + 1 < buf->num_rows) out[off++] = '\n';
    }
    out[off] = '\0';
    if (out_len) *out_len = off;
    return out;
}

static char *sel_to_text(Buffer *buf, const TextSelection *sel,
                         size_t *out_len) {
    YankData yd = yank_data_new(buf, sel);
    if (!yd.rows) return NULL;
    size_t total = 0;
    for (int i = 0; i < yd.num_rows; i++) total += yd.rows[i].len + 1;
    if (total == 0) total = 1;
    char *out = malloc(total + 1);
    if (!out) { yank_data_free(&yd); return NULL; }
    size_t off = 0;
    for (int i = 0; i < yd.num_rows; i++) {
        if (yd.rows[i].len) {
            memcpy(out + off, yd.rows[i].data, yd.rows[i].len);
            off += yd.rows[i].len;
        }
        if (i + 1 < yd.num_rows) out[off++] = '\n';
    }
    out[off] = '\0';
    if (out_len) *out_len = off;
    yank_data_free(&yd);
    return out;
}

static int write_temp(const char *data, size_t len, char *path_out,
                      size_t cap) {
    snprintf(path_out, cap, "/tmp/hed-translate-XXXXXX");
    int fd = mkstemp(path_out);
    if (fd < 0) return -1;
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, data + off, len - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            close(fd); unlink(path_out); return -1;
        }
        off += (size_t)w;
    }
    close(fd);
    return 0;
}

static void insert_lines_from(Buffer *dst, const char *text, size_t len) {
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r'))
        len--;
    size_t start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || text[i] == '\n') {
            size_t llen = i - start;
            if (llen > 0 && text[start + llen - 1] == '\r') llen--;
            buf_row_insert_in(dst, dst->num_rows, text + start, llen);
            start = i + 1;
        }
    }
    if (dst->num_rows == 0)
        buf_row_insert_in(dst, 0, "", 0);
}

static const char *basename_of(const char *path) {
    if (!path || !*path) return "buffer";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* ----- async machinery --------------------------------------------- */

static int out_append(TrJob *j, const char *data, size_t n) {
    if (j->out_len + n + 1 > j->out_cap) {
        size_t ncap = j->out_cap ? j->out_cap * 2 : 4096;
        while (ncap < j->out_len + n + 1) ncap *= 2;
        char *r = realloc(j->out, ncap);
        if (!r) return -1;
        j->out = r;
        j->out_cap = ncap;
    }
    memcpy(j->out + j->out_len, data, n);
    j->out_len += n;
    j->out[j->out_len] = '\0';
    return 0;
}

/* Finalize: reap child, build dst buffer (or surface error), tear down. */
static void job_finalize(TrJob *j) {
    ed_loop_unregister(j->from_fd);
    close(j->from_fd);
    j->from_fd = -1;

    int status = 0;
    if (j->pid > 0) waitpid(j->pid, &status, 0);
    unlink(j->in_path);

    int exit_ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (!exit_ok || j->out_len == 0) {
        ed_set_status_message(
            "translate: trans failed%s (is 'trans' installed?)",
            j->out_len == 0 ? " (empty output)" : "");
        job_active = NULL;
        job_free(j);
        return;
    }

    int idx = -1;
    if (buf_new(NULL, &idx) != ED_OK) {
        ed_set_status_message("translate: failed to create buffer");
        job_active = NULL;
        job_free(j);
        return;
    }
    Buffer *dst = &E.buffers[idx];

    char title[256];
    snprintf(title, sizeof(title), "[translate:%s:%s%s #%d]",
             j->target, j->src_base, j->from_sel ? "(sel)" : "", ++seq);
    free(dst->title);    dst->title    = strdup(title);
    free(dst->filename); dst->filename = NULL;
    free(dst->filetype); dst->filetype = j->src_filetype; /* take ownership */
    j->src_filetype = NULL;

    insert_lines_from(dst, j->out, j->out_len);
    dst->dirty = 0;

    windows_split_vertical();
    Window *w = window_cur();
    if (w) win_attach_buf(w, dst);
    E.current_buffer = idx;
    dst->dirty = 0;

    ed_set_status_message("translate: %s → %s (%d lines)",
                          j->source, j->target, dst->num_rows);
    job_active = NULL;
    job_free(j);
}

static void on_readable(int fd, void *ud) {
    TrJob *j = (TrJob *)ud;
    if (!j || fd != j->from_fd) return;

    for (;;) {
        char chunk[4096];
        ssize_t n = read(fd, chunk, sizeof(chunk));
        if (n > 0) {
            if (out_append(j, chunk, (size_t)n) != 0) {
                /* OOM — kill the child and bail. */
                kill(j->pid, SIGTERM);
                job_finalize(j);
                return;
            }
            continue;
        }
        if (n == 0) {                  /* EOF — trans is done */
            job_finalize(j);
            return;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        /* Real error. */
        job_finalize(j);
        return;
    }
}

/* ----- command ------------------------------------------------------ */

static void cmd_translate(const char *args) {
    if (job_active) {
        ed_set_status_message("translate: already running, please wait");
        return;
    }

    Buffer *src = buf_cur();
    if (!src) { ed_set_status_message("translate: no buffer"); return; }
    Window *win = window_cur();

    char target[16];
    char source[16] = "auto";
    snprintf(target, sizeof(target), "%s", default_target);
    if (args && *args) {
        char tt[16] = {0}, ss[16] = {0};
        int n = sscanf(args, "%15s %15s", tt, ss);
        if (n >= 1 && tt[0]) snprintf(target, sizeof(target), "%s", tt);
        if (n >= 2 && ss[0]) snprintf(source, sizeof(source), "%s", ss);
    }

    size_t in_len = 0;
    char  *in_text = NULL;
    int    from_sel = 0;
    if (win && win->sel.type != SEL_NONE) {
        TextSelection sel;
        if (kb_visual_to_textsel(src, win, 0, &sel)) {
            in_text  = sel_to_text(src, &sel, &in_len);
            from_sel = 1;
        }
    }
    if (!in_text) in_text = buf_to_text(src, &in_len);
    if (!in_text || in_len == 0) {
        free(in_text);
        ed_set_status_message("translate: nothing to translate");
        return;
    }

    TrJob *j = calloc(1, sizeof(*j));
    if (!j) {
        free(in_text);
        ed_set_status_message("translate: OOM");
        return;
    }
    j->from_fd = -1;
    snprintf(j->target, sizeof(j->target), "%s", target);
    snprintf(j->source, sizeof(j->source), "%s", source);
    j->from_sel = from_sel;

    const char *src_ft = (src->filetype && *src->filetype) ? src->filetype
                                                            : "txt";
    j->src_filetype = strdup(src_ft);
    j->src_base     = strdup(basename_of(src->filename ? src->filename
                                                       : src->title));
    if (!j->src_filetype || !j->src_base) {
        free(in_text); job_free(j);
        ed_set_status_message("translate: OOM");
        return;
    }

    if (write_temp(in_text, in_len, j->in_path, sizeof(j->in_path)) != 0) {
        free(in_text); job_free(j);
        ed_set_status_message("translate: failed to write temp input");
        return;
    }
    free(in_text);

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        unlink(j->in_path); job_free(j);
        ed_set_status_message("translate: pipe() failed");
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]); close(pipefd[1]);
        unlink(j->in_path); job_free(j);
        ed_set_status_message("translate: fork() failed");
        return;
    }

    if (pid == 0) {
        /* Child: stdin from /dev/null, stdout to pipe, stderr to log. */
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) { dup2(devnull, STDIN_FILENO); close(devnull); }
        dup2(pipefd[1], STDOUT_FILENO);
        int log_fd = log_fileno();
        if (log_fd >= 0) dup2(log_fd, STDERR_FILENO);
        close(pipefd[0]); close(pipefd[1]);
        execlp("trans", "trans", "-b", "-no-warn",
               "-s", j->source, "-t", j->target, "-i", j->in_path,
               (char *)NULL);
        _exit(127);
    }

    /* Parent. */
    close(pipefd[1]);
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    if (flags >= 0) fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    j->pid     = pid;
    j->from_fd = pipefd[0];
    ed_loop_register("translate", j->from_fd, on_readable, j);

    job_active = j;
    ed_set_status_message("translate: %s → %s ...", source, target);
}

static int translate_init(void) {
    cmd("translate", cmd_translate,
        "translate buffer or selection via 'trans' [target] [source]");
    return 0;
}

const Plugin plugin_translate = {
    .name   = "translate",
    .desc   = "translate buffer/selection with translate-shell (trans)",
    .init   = translate_init,
    .deinit = NULL,
};
