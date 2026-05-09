/* shell plugin: the `:shell` command and its `!` prompt.
 *
 * Two entry points share one execution path:
 *   - `:shell <cmd>`  runs <cmd> directly via shell_execute().
 *   - `:shell` (no args) opens the `!` prompt; on submit, the typed
 *     line is fed to the same shell_execute().
 *
 * Features supported on either path:
 *   - `--skipwait` flag   — skip the post-run "press Enter" wait.
 *   - %p / %d / %n / %b / %y substitution against the current buffer.
 *   - Trailing capture tokens >%b, >>%b, >%v, >%y for splicing
 *     stdout back into the buffer / selection / yank register.
 *
 * The `cmd_shell` symbol is exported (see shell.h) so plugins like
 * treesitter can launch installer commands without re-parsing through
 * the colon command machinery.
 */

#include "hed.h"
#include "prompt.h"
#include "shell.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Internal row helpers from buf/buffer.c — not in the public header
 * but linkable. Used by the >%b / >>%b / >%v capture paths to splice
 * captured stdout under a single undo group. */
extern void buf_row_insert_in(Buffer *buf, int at, const char *s, size_t len);
extern void buf_row_del_in(Buffer *buf, int at);
extern void buf_row_append_in(Buffer *buf, Row *row, const SizedStr *str);

/* ---------- growable string helpers ---------- */

static int sh_append(char **out, size_t *len, size_t *cap,
                     const char *src, size_t n) {
    if (*len + n + 1 > *cap) {
        size_t newcap = *cap ? *cap : 64;
        while (newcap < *len + n + 1)
            newcap *= 2;
        char *p = realloc(*out, newcap);
        if (!p) return 0;
        *out = p;
        *cap = newcap;
    }
    memcpy(*out + *len, src, n);
    *len += n;
    (*out)[*len] = '\0';
    return 1;
}

/* Append a single-quote shell-escaped form of src[0..n) to a growable
 * string. Mirrors shell_escape_single but unbounded. */
static int sh_append_escaped(char **out, size_t *len, size_t *cap,
                             const char *src, size_t n) {
    if (!sh_append(out, len, cap, "'", 1)) return 0;
    for (size_t i = 0; i < n; i++) {
        if (src[i] == '\'') {
            if (!sh_append(out, len, cap, "'\\''", 4)) return 0;
        } else {
            if (!sh_append(out, len, cap, &src[i], 1)) return 0;
        }
    }
    return sh_append(out, len, cap, "'", 1);
}

/* Concatenate buffer rows with '\n', trailing '\n'. Returns malloc'd
 * heap string (NUL-terminated) and writes byte length to *out_len.
 * Caller frees. Returns NULL on OOM or NULL buf. */
static char *buf_text_join(Buffer *buf, size_t *out_len) {
    if (out_len) *out_len = 0;
    if (!buf) return NULL;
    size_t total = 0;
    for (int i = 0; i < buf->num_rows; i++)
        total += buf->rows[i].chars.len + 1;
    char *s = malloc(total + 1);
    if (!s) return NULL;
    size_t off = 0;
    for (int i = 0; i < buf->num_rows; i++) {
        size_t n = buf->rows[i].chars.len;
        if (n) {
            memcpy(s + off, buf->rows[i].chars.data, n);
            off += n;
        }
        s[off++] = '\n';
    }
    s[off] = '\0';
    if (out_len) *out_len = off;
    return s;
}

/* ---------- capture token parsing ---------- */

typedef enum {
    CAP_NONE = 0,
    CAP_REPLACE_BUF,    /* >%b  — replace whole buffer */
    CAP_APPEND_CURSOR,  /* >>%b — splice at cursor */
    CAP_REPLACE_SEL,    /* >%v  — replace visual selection */
    CAP_YANK,           /* >%y  — store stdout in unnamed/yank register */
} CaptureMode;

/* Strip the trailing capture token (if any) by writing a NUL where it
 * begins. Returns the detected mode. Order matters: >>%b is checked
 * before >%b (longer match wins). The token must be preceded by
 * whitespace or start-of-string to qualify. */
static CaptureMode peel_capture_token(char *src) {
    if (!src) return CAP_NONE;
    size_t end = strlen(src);
    while (end > 0 && isspace((unsigned char)src[end - 1])) end--;

    struct { const char *tok; size_t toklen; CaptureMode mode; } table[] = {
        { ">>%b", 4, CAP_APPEND_CURSOR },
        { ">%b",  3, CAP_REPLACE_BUF   },
        { ">%v",  3, CAP_REPLACE_SEL   },
        { ">%y",  3, CAP_YANK          },
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        size_t tl = table[i].toklen;
        if (end < tl) continue;
        if (memcmp(src + end - tl, table[i].tok, tl) != 0) continue;
        size_t before = end - tl;
        if (before != 0 && !isspace((unsigned char)src[before - 1]))
            continue;
        while (before > 0 && isspace((unsigned char)src[before - 1]))
            before--;
        src[before] = '\0';
        return table[i].mode;
    }
    return CAP_NONE;
}

/* ---------- %-token expansion ---------- */

/* Walk src and expand %b/%p/%d/%n/%y (each only at a "word boundary":
 * the next char must be a non-letter or end). %% emits a literal %.
 * Returns malloc'd expanded command, or NULL on OOM. */
static char *expand_shell_template(const char *src, Buffer *buf) {
    if (!src) return NULL;
    size_t total = strlen(src);

    const char *fname = (buf && buf->filename) ? buf->filename : "";
    const char *slash = strrchr(fname, '/');
    const char *base = slash ? slash + 1 : fname;
    /* dirlen: 0 for "foo.c", 3 for "src/foo.c", 1 for "/foo.c" (keep "/"). */
    size_t dirlen = 0;
    if (slash) {
        dirlen = (size_t)(slash - fname);
        if (dirlen == 0) dirlen = 1;
    }

    char *out = NULL;
    size_t len = 0, cap = 0;
    if (!sh_append(&out, &len, &cap, "", 0)) return NULL;

    for (size_t i = 0; i < total; ) {
        if (src[i] != '%') {
            if (!sh_append(&out, &len, &cap, &src[i], 1)) goto oom;
            i++;
            continue;
        }
        if (i + 1 >= total) {
            if (!sh_append(&out, &len, &cap, "%", 1)) goto oom;
            i++;
            continue;
        }
        char tok = src[i + 1];
        char after = (i + 2 < total) ? src[i + 2] : '\0';
        int boundary = !isalpha((unsigned char)after);
        if (tok == '%') {
            if (!sh_append(&out, &len, &cap, "%", 1)) goto oom;
            i += 2;
        } else if (boundary && (tok == 'b' || tok == 'p' ||
                                tok == 'd' || tok == 'n' || tok == 'y')) {
            int ok = 1;
            if (tok == 'b') {
                size_t blen = 0;
                char *txt = buf_text_join(buf, &blen);
                if (!txt) goto oom;
                ok = sh_append_escaped(&out, &len, &cap, txt, blen);
                free(txt);
            } else if (tok == 'p') {
                ok = sh_append_escaped(&out, &len, &cap, fname, strlen(fname));
            } else if (tok == 'd') {
                ok = sh_append_escaped(&out, &len, &cap, fname, dirlen);
            } else if (tok == 'n') {
                ok = sh_append_escaped(&out, &len, &cap, base, strlen(base));
            } else /* 'y' */ {
                const SizedStr *r = regs_get('"');
                const char *ydata = (r && r->data) ? r->data : "";
                size_t ylen = r ? r->len : 0;
                ok = sh_append_escaped(&out, &len, &cap, ydata, ylen);
            }
            if (!ok) goto oom;
            i += 2;
        } else {
            if (!sh_append(&out, &len, &cap, &src[i], 1)) goto oom;
            i++;
        }
    }
    return out;
oom:
    free(out);
    return NULL;
}

/* ---------- buffer splice helpers ---------- */

/* Splice: delete bytes [sy:sx, ey:ex) and insert `lines` at (sy, sx),
 * all within one undo group named `desc`. */
static void splice_lines_at_range(Buffer *buf, int sy, int sx,
                                  int ey, int ex,
                                  char **lines, int count,
                                  const char *desc) {
    if (!buf) return;

    if (buf->num_rows == 0) {
        buf_row_insert_in(buf, 0, "", 0);
        sy = ey = sx = ex = 0;
    }

    if (sy < 0) sy = 0;
    if (sy >= buf->num_rows) sy = buf->num_rows - 1;
    if (ey < sy) ey = sy;
    if (ey >= buf->num_rows) ey = buf->num_rows - 1;
    if (sx < 0) sx = 0;
    if (sx > (int)buf->rows[sy].chars.len) sx = (int)buf->rows[sy].chars.len;
    if (ex < 0) ex = 0;
    if (ex > (int)buf->rows[ey].chars.len) ex = (int)buf->rows[ey].chars.len;
    if (sy == ey && ex < sx) ex = sx;

    undo_begin(buf, desc);

    SizedStr tail = sstr_from(buf->rows[ey].chars.data + ex,
                              buf->rows[ey].chars.len - ex);

    {
        Row *first = &buf->rows[sy];
        undo_record_replace(buf, sy);
        first->chars.len = sx;
        if (first->chars.data)
            first->chars.data[sx] = '\0';
        buf_row_update(first);
    }
    for (int y = ey; y > sy; y--)
        buf_row_del_in(buf, y);

    int end_y = sy, end_x = sx;
    if (count > 0) {
        const char *s0 = lines[0] ? lines[0] : "";
        size_t s0len = strlen(s0);
        if (s0len > 0) {
            SizedStr s = sstr_from(s0, s0len);
            buf_row_append_in(buf, &buf->rows[sy], &s);
            sstr_free(&s);
        }
        for (int i = 1; i < count; i++) {
            const char *s = lines[i] ? lines[i] : "";
            buf_row_insert_in(buf, sy + i, s, strlen(s));
        }
        end_y = sy + count - 1;
        end_x = (int)strlen(lines[count - 1] ? lines[count - 1] : "");
        if (count == 1) end_x = sx + (int)s0len;
    }

    if (tail.len > 0)
        buf_row_append_in(buf, &buf->rows[end_y], &tail);
    sstr_free(&tail);

    if (buf->num_rows == 0)
        buf_row_insert_in(buf, 0, "", 0);

    undo_end(buf);

    if (end_y >= buf->num_rows) end_y = buf->num_rows - 1;
    if (end_y < 0) end_y = 0;
    int row_len = (int)buf->rows[end_y].chars.len;
    if (end_x > row_len) end_x = row_len;
    if (end_x < 0) end_x = 0;
    buf->cursor->y = end_y;
    buf->cursor->x = end_x;
    Window *win = window_cur();
    if (win) {
        win->cursor.y = end_y;
        win->cursor.x = end_x;
    }
}

/* Convert win->sel into byte coordinates (sy, sx, ey, ex) with
 * exclusive end. Returns 1 on success, 0 if the selection isn't
 * usable (NONE, BLOCK, or out-of-bounds). On block, sets *was_block. */
static int sel_to_byte_range(Window *win, Buffer *buf,
                             int *out_sy, int *out_sx,
                             int *out_ey, int *out_ex,
                             int *was_block) {
    if (was_block) *was_block = 0;
    if (!win || !buf) return 0;
    if (win->sel.type == SEL_NONE) return 0;
    if (win->sel.type == SEL_VISUAL_BLOCK) {
        if (was_block) *was_block = 1;
        return 0;
    }
    int ay = win->sel.anchor_y, ax = win->sel.anchor_x;
    int cy = win->cursor.y, cx = win->cursor.x;
    if (ay < 0 || ay >= buf->num_rows) return 0;
    if (cy < 0 || cy >= buf->num_rows) return 0;

    if (win->sel.type == SEL_VISUAL_LINE) {
        int sy = ay < cy ? ay : cy;
        int ey = ay > cy ? ay : cy;
        *out_sy = sy;
        *out_sx = 0;
        if (ey + 1 < buf->num_rows) {
            *out_ey = ey + 1;
            *out_ex = 0;
        } else {
            *out_ey = ey;
            *out_ex = (int)buf->rows[ey].chars.len;
        }
        return 1;
    }

    int sy, sx, ey, ex;
    if (ay < cy || (ay == cy && ax <= cx)) {
        sy = ay; sx = ax; ey = cy; ex = cx;
    } else {
        sy = cy; sx = cx; ey = ay; ex = ax;
    }
    int row_len = (int)buf->rows[ey].chars.len;
    if (ex < row_len) {
        ex++;
    } else if (ey + 1 < buf->num_rows) {
        ey++;
        ex = 0;
    } else {
        ex = row_len;
    }
    *out_sy = sy; *out_sx = sx;
    *out_ey = ey; *out_ex = ex;
    return 1;
}

/* ---------- shared execution path ---------- */

/* Run an already-non-empty command line. Both `:shell <args>` and the
 * `!` prompt's on_submit funnel here. Caller has guaranteed args is
 * non-NULL and not all-whitespace. */
static void shell_execute(const char *args) {
    char cmd_buf[4096];
    snprintf(cmd_buf, sizeof(cmd_buf), "%s", args);

    bool acknowledge = true;
    const char *flag = "--skipwait";
    size_t flen = strlen(flag);
    char *p = cmd_buf;
    while ((p = strstr(p, flag))) {
        char before = (p == cmd_buf) ? ' ' : p[-1];
        char after = p[flen];
        if ((p == cmd_buf || isspace((unsigned char)before)) &&
            (after == '\0' || isspace((unsigned char)after))) {
            acknowledge = false;
            if (p > cmd_buf && isspace((unsigned char)p[-1]))
                p--;
            char *src = p + flen;
            while (isspace((unsigned char)*src))
                src++;
            memmove(p, src, strlen(src) + 1);
            continue;
        }
        p += flen;
    }

    while (isspace((unsigned char)cmd_buf[0]))
        memmove(cmd_buf, cmd_buf + 1, strlen(cmd_buf));
    if (cmd_buf[0] == '\0') {
        ed_set_status_message("Usage: :shell <command>");
        return;
    }

    Buffer *buf = buf_cur();
    Window *win = window_cur();
    CaptureMode capture = peel_capture_token(cmd_buf);

    int sel_sy = 0, sel_sx = 0, sel_ey = 0, sel_ex = 0;
    if (capture == CAP_REPLACE_SEL) {
        int was_block = 0;
        if (!buf || buf->readonly) {
            ed_set_status_message(buf ? "Buffer is read-only"
                                      : ">%v: no buffer");
            return;
        }
        if (!sel_to_byte_range(win, buf, &sel_sy, &sel_sx, &sel_ey, &sel_ex,
                               &was_block)) {
            ed_set_status_message(was_block
                ? ">%v: visual-block selection not supported"
                : ">%v: no visual selection");
            return;
        }
    } else if (capture == CAP_REPLACE_BUF || capture == CAP_APPEND_CURSOR) {
        if (!buf || buf->readonly) {
            ed_set_status_message(buf ? "Buffer is read-only"
                                      : "shell: no buffer to capture into");
            return;
        }
    }

    if (cmd_buf[0] == '\0') {
        ed_set_status_message("Usage: :shell <command>");
        return;
    }

    char *expanded = expand_shell_template(cmd_buf, buf);
    if (!expanded) {
        ed_set_status_message("shell: out of memory expanding command");
        return;
    }

    if (capture != CAP_NONE) {
        char **lines = NULL;
        int count = 0;
        int ok = term_cmd_run(expanded, &lines, &count);
        free(expanded);
        if (!ok) {
            ed_set_status_message("shell: failed to run command");
            return;
        }
        switch (capture) {
        case CAP_REPLACE_BUF: {
            int last = buf->num_rows > 0 ? buf->num_rows - 1 : 0;
            int last_x = (buf->num_rows > 0)
                       ? (int)buf->rows[last].chars.len : 0;
            splice_lines_at_range(buf, 0, 0, last, last_x,
                                  lines, count, "shell-capture");
            if (win) { win->row_offset = 0; win->col_offset = 0; }
            ed_set_status_message("Captured %d line%s into buffer",
                                  count, count == 1 ? "" : "s");
            break;
        }
        case CAP_APPEND_CURSOR: {
            int cy = win ? win->cursor.y : (buf->cursor ? buf->cursor->y : 0);
            int cx = win ? win->cursor.x : (buf->cursor ? buf->cursor->x : 0);
            splice_lines_at_range(buf, cy, cx, cy, cx,
                                  lines, count, "shell-append");
            ed_set_status_message("Inserted %d line%s at cursor",
                                  count, count == 1 ? "" : "s");
            break;
        }
        case CAP_REPLACE_SEL:
            splice_lines_at_range(buf, sel_sy, sel_sx, sel_ey, sel_ex,
                                  lines, count, "shell-replace-sel");
            ed_set_status_message("Replaced selection with %d line%s",
                                  count, count == 1 ? "" : "s");
            break;
        case CAP_YANK: {
            size_t total = 0;
            for (int i = 0; i < count; i++)
                total += (lines[i] ? strlen(lines[i]) : 0) + 1;
            char *joined = malloc(total + 1);
            if (!joined) {
                ed_set_status_message(">%y: out of memory");
                break;
            }
            size_t off = 0;
            for (int i = 0; i < count; i++) {
                size_t n = lines[i] ? strlen(lines[i]) : 0;
                if (n) { memcpy(joined + off, lines[i], n); off += n; }
                joined[off++] = '\n';
            }
            joined[off] = '\0';
            regs_set_yank(joined, off);
            free(joined);
            ed_set_status_message("Yanked %d line%s",
                                  count, count == 1 ? "" : "s");
            break;
        }
        case CAP_NONE: break;
        }
        term_cmd_free(lines, count);
        ed_render_frame();
        return;
    }

    int status = term_cmd_run_interactive(expanded, acknowledge);
    free(expanded);

    if (status == 0) {
        ed_set_status_message("Command completed successfully");
    } else if (status == -1) {
        ed_set_status_message("Failed to run command");
    } else {
        ed_set_status_message("Command exited with status %d", status);
    }

    ed_render_frame();
}

/* ---------- the `!` prompt ---------- */

static const char *shell_label(Prompt *p) {
    (void)p;
    return "!";
}

static void shell_on_submit(Prompt *p, const char *line, int len) {
    (void)p;
    if (!line || len <= 0) return;
    /* Bail on whitespace-only — same UX as cancelling. */
    int i = 0;
    while (i < len && isspace((unsigned char)line[i])) i++;
    if (i >= len) return;
    shell_execute(line);
}

static const PromptVTable shell_vt = {
    .label     = shell_label,
    .on_key    = prompt_default_on_key,
    .on_submit = shell_on_submit,
    .on_cancel = NULL,
    .complete  = NULL,
    .history   = NULL,
};

/* ---------- public entry point + plugin registration ---------- */

void cmd_shell(const char *args) {
    /* No args (or all-whitespace): drop into the `!` prompt. */
    if (!args || !*args) {
        prompt_open(&shell_vt, NULL);
        return;
    }
    const char *p = args;
    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p) {
        prompt_open(&shell_vt, NULL);
        return;
    }
    shell_execute(args);
}

static int shell_init(void) {
    cmd("shell", cmd_shell, "run shell cmd");
    return 0;
}

const Plugin plugin_shell = {
    .name   = "shell",
    .desc   = "`:shell` command and `!` prompt",
    .init   = shell_init,
    .deinit = NULL,
};
