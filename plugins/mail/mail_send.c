/* mail_send: compose + send outgoing mail.
 *
 * `mail_compose()` opens a fresh editable buffer pre-filled with the
 * standard headers and lands in insert mode at the To: line.
 * `mail_send_current()` pipes the active buffer to the configured
 * send command (default `msmtp -t`) which is expected to read an
 * RFC 822 message on stdin and route it from the To/Cc/Bcc headers. */

#include "buf/row.h"
#include "hed.h"
#include "mail.h"
#include "mail_internal.h"
#include "pickers/fzf.h"
#include "utils/term_cmd.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static char send_cmd[256] = "msmtp -t";
static char from_addr[256] = "";
static int compose_seq = 0;

void mail_set_send_cmd(const char *cmd) {
    snprintf(send_cmd, sizeof(send_cmd), "%s",
             (cmd && *cmd) ? cmd : "msmtp -t");
}

void mail_set_from(const char *from) {
    snprintf(from_addr, sizeof(from_addr), "%s", from ? from : "");
}

const char *mail_get_from(void) { return from_addr; }

/* ------------------------------------------------------------------ */
/* Line-array helpers (stb_ds arrays of malloc'd strings)              */
/* ------------------------------------------------------------------ */

/* printf a line onto a growing line array. Header/label lines only —
 * anything that can exceed the format buffer is pushed raw. */
static void pushf(char ***lines, const char *fmt, ...) {
    char buf[1280];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    char *dup = strdup(buf);
    if (dup)
        arrput(*lines, dup);
}

static void free_lines(char **lines) {
    for (ptrdiff_t i = 0; i < arrlen(lines); i++)
        free(lines[i]);
    arrfree(lines);
}

/* Fresh compose buffer (never reused): mail://compose-N. Highlighting
 * via mail_msg_render_hook (registered for filetypes mail-message and
 * mail-compose). */
static int compose_new_buf(const char *title) {
    char bufname[64];
    snprintf(bufname, sizeof(bufname), "mail://compose-%d", ++compose_seq);
    BufSpecial spec = {.name = bufname,
                       .title = title,
                       .filetype = "mail-compose",
                       .as_filename = 1};
    int idx = buf_special_get(&spec, NULL);
    if (idx < 0)
        ed_set_status_message("mail: failed to open compose buffer");
    return idx;
}

void mail_compose(void) {
    int idx = compose_new_buf("Compose");
    if (idx < 0)
        return;

    Buffer *buf = &E.buffers[idx];
    buf_special_addf(buf, "From: %s", from_addr);
    buf_special_addf(buf, "To: ");
    buf_special_addf(buf, "Cc: ");
    buf_special_addf(buf, "Subject: ");
    buf_special_add(buf, "", 0);
    buf_special_add(buf, "", 0);
    buf_special_show(idx);

    Window *win = window_cur();
    if (win) {
        /* Land on the To: line, after the prefix. */
        win->cursor.y = 1;
        win->cursor.x = 4;
    }

    ed_set_mode(MODE_INSERT);
    ed_set_status_message(
        "mail: compose — edit headers + body, C-c C-c or :mail-send to send");
}

/* Create a fresh compose buffer, fill from lines, attach to the
 * current window, set insert mode at body. Public — the git-patch
 * plugin feeds fully-formed messages through this. */
void mail_compose_with_lines(const char *title, char **lines, int count) {
    int idx = compose_new_buf(title);
    if (idx < 0)
        return;

    Buffer *buf = &E.buffers[idx];
    int body_row = -1;
    for (int i = 0; i < count; i++) {
        const char *s = lines[i] ? lines[i] : "";
        buf_special_add(buf, s, strlen(s));
        if (body_row < 0 && s[0] == '\0')
            body_row = i + 1;
    }
    if (body_row < 0)
        body_row = buf->num_rows;
    buf_special_show(idx);

    Window *win = window_cur();
    if (win) {
        win->cursor.y = body_row;
        win->cursor.x = 0;
    }
    ed_set_mode(MODE_INSERT);
}

/* ------------------------------------------------------------------ */
/* Attaching files to a compose buffer                                 */
/* ------------------------------------------------------------------ */

/* Insert "Attach: <path>" as the last line of the header block (just
 * before the blank separator; appended at EOF if there is none).
 * Returns 0 on success. */
static int insert_attach_header(Buffer *buf, const char *path) {
    char line[PATH_MAX + 16];
    snprintf(line, sizeof(line), "Attach: %s", path);
    int at = buf->num_rows;
    for (int i = 0; i < buf->num_rows; i++) {
        if (buf->rows[i].chars.len == 0) {
            at = i;
            break;
        }
    }
    int before = buf->num_rows;
    buf_row_insert_in(buf, at, line, strlen(line));
    return buf->num_rows == before;
}

void mail_attach_add(const char *path) {
    Buffer *buf = buf_cur();
    if (!buf || !buf->filetype || strcmp(buf->filetype, "mail-compose") != 0) {
        ed_set_status_message("mail-attach-add: open a compose buffer first");
        return;
    }

    /* Explicit path: expand ~, validate, insert. */
    if (path && *path) {
        char full[PATH_MAX];
        str_expand_tilde(path, full, sizeof(full));
        if (access(full, R_OK) != 0) {
            ed_set_status_message("mail-attach-add: cannot read %s", full);
            return;
        }
        if (insert_attach_header(buf, full)) {
            ed_set_status_message("mail-attach-add: out of memory");
            return;
        }
        ed_set_status_message("mail-attach-add: attached %s", full);
        return;
    }

    /* No path: fzf multi-pick over project files (Tab to select). */
    char **sel = NULL;
    int cnt = 0;
    if (!fzf_run_opts(FZF_PROJECT_FILES_CMD,
                      "--preview '" FZF_FILE_PREVIEW_BODY
                      "' --preview-window right,60%,wrap",
                      1, &sel, &cnt) ||
        cnt <= 0) {
        fzf_free(sel, cnt);
        ed_set_status_message("mail-attach-add: canceled");
        return;
    }

    int ok = 0, fail = 0;
    for (int i = 0; i < cnt; i++) {
        if (!sel[i] || !sel[i][0])
            continue;
        if (access(sel[i], R_OK) != 0 || insert_attach_header(buf, sel[i]))
            fail++;
        else
            ok++;
    }
    fzf_free(sel, cnt);

    if (fail > 0)
        ed_set_status_message("mail-attach-add: attached %d, %d failed", ok,
                              fail);
    else
        ed_set_status_message("mail-attach-add: attached %d file(s)", ok);
}

/* Find the header line "<Name>: <value>" in buf (headers stop at the
 * first blank line) and copy the value, trimmed of leading whitespace,
 * into out. Returns 1 on hit (out may still be "" for an empty value). */
static int header_value(Buffer *buf, const char *name, char *out, size_t cap) {
    size_t nlen = strlen(name);
    for (int i = 0; i < buf->num_rows; i++) {
        StrBuf *s = &buf->rows[i].chars;
        if (s->len == 0)
            return 0; /* end of headers */
        if (s->len <= nlen)
            continue;
        if (strncasecmp(s->data, name, nlen) != 0)
            continue;
        if (s->data[nlen] != ':')
            continue;
        size_t k = nlen + 1;
        while (k < s->len && (s->data[k] == ' ' || s->data[k] == '\t'))
            k++;
        size_t vlen = s->len - k;
        if (vlen >= cap)
            vlen = cap - 1;
        memcpy(out, s->data + k, vlen);
        out[vlen] = '\0';
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* MIME multipart helpers (used when Attach: headers are present)      */
/* ------------------------------------------------------------------ */

static const char b64chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* base64-encode `in` onto `out`, line-wrapped at 76 columns. Returns
 * 0 on success, non-zero on read error. */
static int base64_stream(FILE *in, FILE *out) {
    unsigned char buf[3];
    int col = 0;
    size_t got;
    while ((got = fread(buf, 1, 3, in)) > 0) {
        unsigned long v = ((unsigned long)buf[0] << 16) |
                          (got > 1 ? (unsigned long)buf[1] << 8 : 0) |
                          (got > 2 ? (unsigned long)buf[2] : 0);
        char enc[4] = {
            b64chars[(v >> 18) & 0x3f],
            b64chars[(v >> 12) & 0x3f],
            got > 1 ? b64chars[(v >> 6) & 0x3f] : '=',
            got > 2 ? b64chars[v & 0x3f] : '=',
        };
        if (fwrite(enc, 1, 4, out) != 4)
            return 1;
        col += 4;
        if (col >= 76) {
            fputc('\n', out);
            col = 0;
        }
    }
    if (col > 0)
        fputc('\n', out);
    return ferror(in);
}

/* Return the basename component of `path` (pointer into path). */
static const char *path_basename(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* Sniff the mime type via `file --mime-type -b <path>`. Falls back to
 * application/octet-stream when the file command is unavailable. */
static void sniff_mime_type(const char *path, char *out, size_t cap) {
    snprintf(out, cap, "application/octet-stream");
    char pq[1280];
    shell_escape_single(path, pq, sizeof(pq));
    char cmd[1400];
    snprintf(cmd, sizeof(cmd), "file --mime-type -b -- %s 2>/dev/null", pq);

    char **lines = NULL;
    int n = 0;
    if (!term_cmd_capture(cmd, &lines, &n) || n == 0) {
        term_cmd_free(lines, n);
        return;
    }
    char trimmed[256];
    str_trim_whitespace(lines[0], trimmed, sizeof(trimmed));
    if (trimmed[0] && strchr(trimmed, '/'))
        safe_strcpy(out, trimmed, cap);
    term_cmd_free(lines, n);
}

/* Collect the values of the Attach: pseudo-headers in buf's header
 * block as an stb_ds array of malloc'd paths. */
static char **collect_attach_paths(Buffer *buf) {
    char **paths = NULL;
    for (int i = 0; i < buf->num_rows; i++) {
        StrBuf *s = &buf->rows[i].chars;
        if (s->len == 0)
            break; /* end of headers */
        if (s->len <= 7 || strncasecmp(s->data, "Attach:", 7) != 0)
            continue;
        size_t k = 7;
        while (k < s->len && (s->data[k] == ' ' || s->data[k] == '\t'))
            k++;
        size_t vlen = s->len - k;
        if (vlen == 0)
            continue;
        char *p = malloc(vlen + 1);
        if (!p)
            break;
        memcpy(p, s->data + k, vlen);
        p[vlen] = '\0';
        arrput(paths, p);
    }
    return paths;
}

/* Write the headers of buf to fp, suppressing any Attach: lines, and
 * inserting MIME-Version + Content-Type:multipart/mixed when
 * boundary != NULL. Stops at the blank line that ends the header
 * block (which is also written). */
static int write_headers_with_mime(Buffer *buf, FILE *fp,
                                   const char *boundary) {
    int wrote_mime = 0;
    for (int i = 0; i < buf->num_rows; i++) {
        StrBuf *s = &buf->rows[i].chars;
        /* Skip Attach: pseudo-headers — they're consumed into the
         * MIME envelope, not emitted to the wire. */
        if (s->len > 7 && strncasecmp(s->data, "Attach:", 7) == 0)
            continue;
        if (boundary && !wrote_mime && s->len == 0) {
            fprintf(fp, "MIME-Version: 1.0\n");
            fprintf(fp, "Content-Type: multipart/mixed; boundary=\"%s\"\n",
                    boundary);
            wrote_mime = 1;
        }
        if (s->len)
            fwrite(s->data, 1, s->len, fp);
        fputc('\n', fp);
        if (s->len == 0)
            return i + 1; /* body starts on next row */
    }
    return buf->num_rows;
}

/* Write a multipart-MIME version of buf to fp. Returns 0 on success. */
static int write_multipart(Buffer *buf, FILE *fp, char **att_paths,
                           const char *boundary) {
    int body_start = write_headers_with_mime(buf, fp, boundary);

    /* Text body part. */
    fprintf(fp, "--%s\n", boundary);
    fprintf(fp, "Content-Type: text/plain; charset=utf-8\n");
    fprintf(fp, "Content-Transfer-Encoding: 8bit\n\n");
    for (int i = body_start; i < buf->num_rows; i++) {
        StrBuf *s = &buf->rows[i].chars;
        if (s->len)
            fwrite(s->data, 1, s->len, fp);
        fputc('\n', fp);
    }

    /* One part per attachment. */
    for (ptrdiff_t i = 0; i < arrlen(att_paths); i++) {
        const char *path = att_paths[i];
        FILE *af = fopen(path, "rb");
        if (!af)
            return 1;
        const char *base = path_basename(path);
        fprintf(fp, "--%s\n", boundary);
        if (str_ends_with(base, ".eml")) {
            /* A forwarded message travels as message/rfc822, which
             * RFC 2046 allows only in 7bit/8bit/binary — copied
             * through verbatim, never base64. */
            fprintf(fp, "Content-Type: message/rfc822; name=\"%s\"\n", base);
            fprintf(fp, "Content-Disposition: attachment; filename=\"%s\"\n",
                    base);
            fprintf(fp, "Content-Transfer-Encoding: 8bit\n\n");
            char chunk[8192];
            size_t got;
            int last = '\n';
            while ((got = fread(chunk, 1, sizeof(chunk), af)) > 0) {
                if (fwrite(chunk, 1, got, fp) != got) {
                    fclose(af);
                    return 1;
                }
                last = chunk[got - 1];
            }
            int rc = ferror(af);
            fclose(af);
            if (rc)
                return rc;
            if (last != '\n')
                fputc('\n', fp);
            continue;
        }
        char mime[128];
        sniff_mime_type(path, mime, sizeof(mime));
        fprintf(fp, "Content-Type: %s; name=\"%s\"\n", mime, base);
        fprintf(fp, "Content-Disposition: attachment; filename=\"%s\"\n", base);
        fprintf(fp, "Content-Transfer-Encoding: base64\n\n");
        int rc = base64_stream(af, fp);
        fclose(af);
        if (rc)
            return rc;
    }

    fprintf(fp, "--%s--\n", boundary);
    return 0;
}

void mail_send_current(void) {
    Buffer *buf = buf_cur();
    if (!buf) {
        ed_set_status_message("mail-send: no buffer");
        return;
    }
    if (buf->num_rows == 0) {
        ed_set_status_message("mail-send: empty buffer");
        return;
    }
    char hv[16];
    if (!header_value(buf, "To", hv, sizeof(hv)) || !hv[0]) {
        ed_set_status_message("mail-send: missing To: header");
        return;
    }
    if (!header_value(buf, "Subject", hv, sizeof(hv)) || !hv[0]) {
        ed_set_status_message("mail-send: missing Subject: header");
        return;
    }

    /* Collect Attach: pseudo-headers. If any are present we emit a
     * multipart/mixed envelope; otherwise the message goes out as
     * plain text exactly as before. */
    char **att_paths = collect_attach_paths(buf);

    char tmpl[PATH_MAX];
    if (fs_temp_path("hed-mail", tmpl, sizeof(tmpl)) != ED_OK) {
        free_lines(att_paths);
        ed_set_status_message("mail-send: failed to reserve temp file");
        return;
    }
    FILE *fp = fopen(tmpl, "w");
    if (!fp) {
        fs_unlink(tmpl);
        free_lines(att_paths);
        ed_set_status_message("mail-send: failed to open temp file");
        return;
    }

    int wr_err = 0;
    if (arrlen(att_paths) > 0) {
        char boundary[64];
        snprintf(boundary, sizeof(boundary), "=_hed_%d_%ld", (int)getpid(),
                 (long)time(NULL));
        wr_err = write_multipart(buf, fp, att_paths, boundary);
    } else {
        for (int i = 0; i < buf->num_rows; i++) {
            StrBuf *s = &buf->rows[i].chars;
            if (s->len)
                fwrite(s->data, 1, s->len, fp);
            fputc('\n', fp);
        }
    }
    if (fflush(fp) != 0 || ferror(fp))
        wr_err = 1;
    fclose(fp);
    free_lines(att_paths);
    if (wr_err) {
        fs_unlink(tmpl);
        ed_set_status_message("mail-send: failed to write message");
        return;
    }

    char shell_cmd[PATH_MAX + 512];
    snprintf(shell_cmd, sizeof(shell_cmd), "%s < %s", send_cmd, tmpl);
    int rc = term_cmd_system(shell_cmd);
    fs_unlink(tmpl);
    if (rc != 0) {
        if (WIFEXITED(rc))
            ed_set_status_message("mail-send: %s exited %d", send_cmd,
                                  WEXITSTATUS(rc));
        else if (WIFSIGNALED(rc))
            ed_set_status_message("mail-send: %s killed by signal %d", send_cmd,
                                  WTERMSIG(rc));
        else
            ed_set_status_message("mail-send: %s failed (status %d)", send_cmd,
                                  rc);
        return;
    }

    buf->dirty = 0;
    ed_set_status_message("mail-send: sent via %s", send_cmd);
}

/* ------------------------------------------------------------------ */
/* Reply / Forward                                                     */
/* ------------------------------------------------------------------ */
/* The mail-message buffer's filename is "mail://<thread:...>".
 * Return a pointer to the "thread:..." part, or NULL if not a message. */
static const char *current_thread_id(Buffer *buf) {
    if (!buf || !buf->filename)
        return NULL;
    if (!buf->filetype || strcmp(buf->filetype, "mail-message") != 0)
        return NULL;
    const char *fn = buf->filename;
    if (strncmp(fn, "mail://", 7) != 0)
        return NULL;
    return fn + 7;
}

/* ------------------------------------------------------------------ */
/* The reply box: typing under the conversation                        */
/* ------------------------------------------------------------------ */

/* In a thread — the chat view especially — a reply wants no buffer of
 * its own: the conversation is right there, and what is missing is
 * somewhere to type. `i` opens a box under the last message. Only the
 * headers come from `hml reply` (the quote is skipped whatever
 * :mail-quote says: the thread is above the box), the body is what
 * you type, and C-c C-c sends it through the same command as any
 * other message. C-c C-k throws it away.
 *
 * The rows are the mail plugin's own rendering, so nothing is saved
 * and nothing is lost: on send or cancel the thread is re-read from
 * hml and the box is gone. */
static struct {
    char bufname[256]; /* the thread buffer the box belongs to */
    char **hdrs;       /* the reply headers, stb_ds */
    int sep_row;       /* the divider; the body is everything below */
} draft;

static void draft_clear(void) {
    free_lines(draft.hdrs);
    draft.hdrs = NULL;
    draft.bufname[0] = '\0';
    draft.sep_row = -1;
}

/* The buffer the open box belongs to, or NULL. */
static Buffer *draft_buffer(void) {
    if (!draft.bufname[0])
        return NULL;
    int idx = buf_find_by_filename(draft.bufname);
    if (idx < 0) {
        draft_clear();
        return NULL;
    }
    Buffer *b = &E.buffers[idx];
    if (draft.sep_row < 0 || draft.sep_row >= b->num_rows) {
        draft_clear();
        return NULL;
    }
    return b;
}

/* Take the box away and hand the buffer back to the renderer. */
static void draft_close(Buffer *buf) {
    if (buf) {
        buf_special_trim(buf, draft.sep_row);
        buf->readonly = 1;
        buf->dirty = 0;
    }
    draft_clear();
    ed_set_mode(MODE_NORMAL);
}

void mail_reply_inline(void) {
    Buffer *buf = buf_cur();
    const char *tid = current_thread_id(buf);
    if (!tid) {
        ed_set_status_message("mail-reply-inline: open a thread first");
        return;
    }
    if (draft_buffer() == buf) { /* already open: just go there */
        Window *w = window_cur();
        if (w)
            w->cursor.y = buf->num_rows - 1;
        ed_set_mode(MODE_INSERT);
        return;
    }
    draft_clear();

    /* A reply in a conversation answers the other party's last
     * message, wherever the cursor is. When the whole thread is your
     * own — you wrote and nobody has answered — answer your own last
     * message but keep writing to whoever it went to. */
    const MailMsgSpan *last = mail_msg_newest_other();
    const char *keep_to = NULL;
    if (!last) {
        last = mail_msg_newest();
        if (last && last->to[0])
            keep_to = last->to;
    }
    char query[400];
    if (last && last->msg_id[0])
        snprintf(query, sizeof(query), "id:%s", last->msg_id);
    else
        snprintf(query, sizeof(query), "%s", tid);
    char qq[512], cmd[600];
    shell_escape_single(query, qq, sizeof(qq));
    snprintf(cmd, sizeof(cmd), "hml reply --reply-to=sender -- %s 2>/dev/null",
             qq);

    char **lines = NULL;
    int count = 0;
    term_cmd_capture(cmd, &lines, &count);
    if (count == 0) {
        term_cmd_free(lines, count);
        ed_set_status_message("mail-reply-inline: hml reply gave nothing");
        return;
    }
    char to[600] = "";
    for (int i = 0; i < count; i++) {
        if (!lines[i] || !lines[i][0])
            break; /* the header block ends at the first empty line */
        if (from_addr[0] && strncasecmp(lines[i], "From:", 5) == 0) {
            pushf(&draft.hdrs, "From: %s", from_addr);
        } else if (keep_to && strncasecmp(lines[i], "To:", 3) == 0) {
            pushf(&draft.hdrs, "To: %s", keep_to);
            snprintf(to, sizeof(to), "%s", keep_to);
        } else {
            arrput(draft.hdrs, strdup(lines[i]));
            if (strncasecmp(lines[i], "To:", 3) == 0)
                snprintf(to, sizeof(to), "%s", lines[i] + 3);
        }
    }
    term_cmd_free(lines, count);
    if (arrlen(draft.hdrs) == 0) {
        draft_clear();
        ed_set_status_message("mail-reply-inline: no reply headers");
        return;
    }

    snprintf(draft.bufname, sizeof(draft.bufname), "%s", buf->filename);
    buf_special_add(buf, "", 0);
    draft.sep_row = buf->num_rows - 1;
    const char *who = to[0] ? to : "the sender";
    while (*who == ' ')
        who++;
    buf_special_addf(buf,
                     "──── reply to %s ─── C-c C-c sends, C-c C-k "
                     "discards ────",
                     who);
    buf_special_add(buf, "", 0);
    buf->readonly = 0;
    buf->dirty = 0;

    Window *win = window_cur();
    if (win) {
        win->cursor.y = buf->num_rows - 1;
        win->cursor.x = 0;
    }
    ed_set_mode(MODE_INSERT);
    ed_set_status_message("mail: reply to %s — C-c C-c sends", who);
}

void mail_reply_inline_cancel(void) {
    Buffer *buf = draft_buffer();
    if (!buf) {
        ed_set_status_message("mail: no reply box open");
        return;
    }
    draft_close(buf);
    ed_set_status_message("mail: reply discarded");
}

void mail_reply_inline_send(void) {
    Buffer *buf = draft_buffer();
    if (!buf || buf != buf_cur()) {
        ed_set_status_message("mail: no reply box open here");
        return;
    }
    /* Everything below the divider is the message. */
    int body = draft.sep_row + 2, len = 0;
    for (int i = body; i < buf->num_rows; i++)
        len += (int)buf->rows[i].chars.len;
    if (len == 0) {
        ed_set_status_message("mail: the reply is empty");
        return;
    }

    char tmpl[PATH_MAX];
    if (fs_temp_path("hed-mail", tmpl, sizeof(tmpl)) != ED_OK) {
        ed_set_status_message("mail: failed to reserve a temp file");
        return;
    }
    FILE *fp = fopen(tmpl, "w");
    if (!fp) {
        fs_unlink(tmpl);
        ed_set_status_message("mail: failed to open a temp file");
        return;
    }
    for (ptrdiff_t i = 0; i < arrlen(draft.hdrs); i++)
        fprintf(fp, "%s\n", draft.hdrs[i]);
    fputc('\n', fp);
    for (int i = body; i < buf->num_rows; i++) {
        StrBuf *r = &buf->rows[i].chars;
        if (r->len)
            fwrite(r->data, 1, r->len, fp);
        fputc('\n', fp);
    }
    int wr_err = fflush(fp) != 0 || ferror(fp);
    fclose(fp);
    if (wr_err) {
        fs_unlink(tmpl);
        ed_set_status_message("mail: failed to write the message");
        return;
    }

    char shell_cmd[PATH_MAX + 512];
    snprintf(shell_cmd, sizeof(shell_cmd), "%s < %s", send_cmd, tmpl);
    int rc = term_cmd_system(shell_cmd);
    fs_unlink(tmpl);
    if (rc != 0) {
        ed_set_status_message("mail: %s exited %d — the reply is still here",
                              send_cmd, WIFEXITED(rc) ? WEXITSTATUS(rc) : rc);
        return;
    }
    draft_close(buf);
    mail_thread_refresh();
    ed_set_status_message("mail: sent");
}

/* ------------------------------------------------------------------ */
/* mailto: URI (RFC 6068) → compose                                    */
/* ------------------------------------------------------------------ */

static int hexval(int c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* Percent-decode `src` (length `len`) into a fresh malloc'd NUL-terminated
 * string. `form` non-zero also decodes '+' as space (tolerant — RFC 6068
 * mandates %20, but many producers still emit '+'). Returns NULL on OOM. */
static char *url_decode(const char *src, size_t len, int form) {
    char *out = malloc(len + 1);
    if (!out)
        return NULL;
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        char c = src[i];
        if (c == '%' && i + 2 < len) {
            int hi = hexval((unsigned char)src[i + 1]);
            int lo = hexval((unsigned char)src[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out[j++] = (char)((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        if (form && c == '+') {
            out[j++] = ' ';
            continue;
        }
        out[j++] = c;
    }
    out[j] = '\0';
    return out;
}

/* Append `add` to the comma-separated list in `*dst` (malloc'd). Skips
 * empty additions. Frees and replaces `*dst`. */
static void append_csv(char **dst, const char *add) {
    if (!add || !*add)
        return;
    if (!*dst || !**dst) {
        free(*dst);
        *dst = strdup(add);
        return;
    }
    size_t need = strlen(*dst) + 2 + strlen(add) + 1;
    char *nw = malloc(need);
    if (!nw)
        return;
    snprintf(nw, need, "%s, %s", *dst, add);
    free(*dst);
    *dst = nw;
}

void mail_compose_uri(const char *uri) {
    if (!uri)
        return;
    if (strncmp(uri, "mailto:", 7) != 0) {
        ed_set_status_message("mail: not a mailto: URI");
        return;
    }

    const char *rest = uri + 7;
    const char *q = strchr(rest, '?');
    size_t path_len = q ? (size_t)(q - rest) : strlen(rest);

    char *to_acc = NULL;
    char *cc_acc = NULL;
    char *bcc_acc = NULL;
    char *subject = NULL;
    char *body = NULL;
    char *in_reply_to = NULL;
    char *references = NULL;

    /* Path: comma-list of recipients. Each recipient is percent-decoded
     * individually so commas inside encoded display-names (%2C) survive. */
    if (path_len > 0) {
        size_t start = 0;
        for (size_t i = 0; i <= path_len; i++) {
            if (i == path_len || rest[i] == ',') {
                char *dec = url_decode(rest + start, i - start, 0);
                if (dec && *dec)
                    append_csv(&to_acc, dec);
                free(dec);
                start = i + 1;
            }
        }
    }

    /* Query: &-separated key=value pairs. */
    if (q) {
        const char *p = q + 1;
        while (*p) {
            const char *amp = strchr(p, '&');
            size_t seg = amp ? (size_t)(amp - p) : strlen(p);
            const char *eq = memchr(p, '=', seg);
            if (eq) {
                size_t klen = (size_t)(eq - p);
                size_t vlen = seg - klen - 1;
                char *key = url_decode(p, klen, 0);
                char *val = url_decode(eq + 1, vlen, 1);
                if (key && val) {
                    if (!strcasecmp(key, "to"))
                        append_csv(&to_acc, val);
                    else if (!strcasecmp(key, "cc"))
                        append_csv(&cc_acc, val);
                    else if (!strcasecmp(key, "bcc"))
                        append_csv(&bcc_acc, val);
                    else if (!strcasecmp(key, "subject")) {
                        free(subject);
                        subject = strdup(val);
                    } else if (!strcasecmp(key, "body")) {
                        free(body);
                        body = strdup(val);
                    } else if (!strcasecmp(key, "in-reply-to")) {
                        free(in_reply_to);
                        in_reply_to = strdup(val);
                    } else if (!strcasecmp(key, "references")) {
                        free(references);
                        references = strdup(val);
                    }
                    /* unknown keys: ignore per RFC 6068 §4 */
                }
                free(key);
                free(val);
            }
            p = amp ? amp + 1 : p + seg;
        }
    }

    /* Build the line array. */
    char **lines = NULL;
    pushf(&lines, "From: %s", from_addr);
    pushf(&lines, "To: %s", to_acc ? to_acc : "");
    pushf(&lines, "Cc: %s", cc_acc ? cc_acc : "");
    if (bcc_acc && *bcc_acc)
        pushf(&lines, "Bcc: %s", bcc_acc);
    pushf(&lines, "Subject: %s", subject ? subject : "");
    if (in_reply_to && *in_reply_to)
        pushf(&lines, "In-Reply-To: %s", in_reply_to);
    if (references && *references)
        pushf(&lines, "References: %s", references);

    /* Header/body separator. */
    arrput(lines, strdup(""));

    /* Body: split on CR/LF in either order. */
    if (body && *body) {
        char *p = body;
        char *seg = body;
        while (*p) {
            if (*p == '\n' || *p == '\r') {
                char save = *p;
                *p = '\0';
                arrput(lines, strdup(seg));
                if (save == '\r' && p[1] == '\n')
                    p++;
                p++;
                seg = p;
            } else {
                p++;
            }
        }
        if (*seg)
            arrput(lines, strdup(seg));
    } else {
        arrput(lines, strdup(""));
    }

    mail_compose_with_lines("Compose (mailto)", lines, (int)arrlen(lines));

    free_lines(lines);
    free(to_acc);
    free(cc_acc);
    free(bcc_acc);
    free(subject);
    free(body);
    free(in_reply_to);
    free(references);

    ed_set_status_message("mail: compose from mailto: URI");
}

void mail_reply(int reply_all) {
    Buffer *src = buf_cur();
    const char *tid = current_thread_id(src);
    if (!tid) {
        ed_set_status_message("mail-reply: open a message first");
        return;
    }

    /* Answer the message under the cursor, not the thread's newest
     * (which is what a thread: query would give hml reply). */
    int midx = 0, mcount = 0;
    const MailMsgSpan *m = mail_cursor_msg(&midx, &mcount);
    char query[400];
    if (m && m->msg_id[0])
        snprintf(query, sizeof(query), "id:%s", m->msg_id);
    else
        snprintf(query, sizeof(query), "%s", tid);
    char qq[512];
    shell_escape_single(query, qq, sizeof(qq));
    char cmd[600];
    snprintf(cmd, sizeof(cmd), "hml reply --reply-to=%s -- %s 2>/dev/null",
             reply_all ? "all" : "sender", qq);

    char **lines = NULL;
    int count = 0;
    term_cmd_capture(cmd, &lines, &count);
    if (count == 0) {
        ed_set_status_message("mail-reply: hml reply produced no output");
        term_cmd_free(lines, count);
        return;
    }

    /* The quote is only the template: hml prints the headers, a blank
     * line, then "On … wrote:" and the message indented. Dropping
     * everything from that blank line leaves the headers that make it
     * a reply — In-Reply-To, References, To — and an empty body to
     * type in. Nothing else changes: :mail-send sends the buffer. */
    if (!mail_quoting()) {
        int hdr = count;
        for (int i = 0; i < count; i++) {
            if (lines[i] && lines[i][0] == '\0') {
                hdr = i;
                break;
            }
        }
        for (int i = hdr; i < count; i++)
            free(lines[i]);
        count = hdr;
        char **grown = realloc(lines, (size_t)(count + 2) * sizeof(char *));
        if (grown) {
            lines = grown;
            lines[count++] = strdup(""); /* the header/body separator */
            lines[count++] = strdup("");
        }
    }

    /* If a from_addr is configured, override hml's From: line. */
    if (from_addr[0]) {
        for (int i = 0; i < count; i++) {
            if (lines[i] && strncasecmp(lines[i], "From:", 5) == 0) {
                free(lines[i]);
                size_t need = 6 + strlen(from_addr) + 1;
                lines[i] = malloc(need);
                if (lines[i])
                    snprintf(lines[i], need, "From: %s", from_addr);
                break;
            }
        }
    }

    mail_compose_with_lines(reply_all ? "Reply-All" : "Reply", lines, count);
    term_cmd_free(lines, count);
    ed_set_status_message(
        "mail-reply: %s to message %d/%d — edit body, C-c C-c or :mail-send "
        "to send",
        reply_all ? "reply-all" : "sender", mail_msg_number(midx, mcount),
        mcount);
}

/* The cursor message of the viewed thread, or NULL with a status
 * message. `*midx`/`*mcount` as in mail_cursor_msg. */
static const MailMsgSpan *forward_source(const char *what, int *midx,
                                         int *mcount) {
    Buffer *src = buf_cur();
    if (!current_thread_id(src)) {
        ed_set_status_message("%s: open a message first", what);
        return NULL;
    }
    const MailMsgSpan *m = mail_cursor_msg(midx, mcount);
    if (!m) {
        ed_set_status_message("%s: no message under the cursor", what);
        return NULL;
    }
    return m;
}

/* "Fwd: <subject>" without double-prefixing. */
static void fwd_subject(const char *orig, char *out, size_t cap) {
    int already =
        (strncasecmp(orig, "Fwd:", 4) == 0 || strncasecmp(orig, "Fw:", 3) == 0);
    snprintf(out, cap, "%s%s", already ? "" : "Fwd: ", orig);
}

void mail_forward(void) {
    int midx = 0, mcount = 0;
    const MailMsgSpan *m = forward_source("mail-forward", &midx, &mcount);
    if (!m)
        return;
    Buffer *src = buf_cur();

    /* The original headers (From / Date / Subject / To / Cc) come
     * from the parsed message span — the same in either thread view.
     * The body is what the user is reading: this message's rows from
     * its first body row up to the next message block (or EOF), minus
     * trailing blank lines. In the chat view that is the stripped
     * body (no quotes / signature). */
    const char *orig_from = m->from, *orig_date = m->date;
    const char *orig_subj = m->subject, *orig_to = m->to, *orig_cc = m->cc;
    int body_start = m->body_row;
    int body_end = src->num_rows;
    {
        int cnt = 0;
        const MailMsgSpan *next = NULL;
        mail_cursor_msg(NULL, &cnt);
        if (midx + 1 < cnt)
            next = m + 1;
        if (next)
            body_end = next->row;
    }
    while (body_end > body_start && body_end > 0 &&
           src->rows[body_end - 1].chars.len == 0)
        body_end--;

    /* Extract this message's attachments into /tmp; each becomes an
     * `Attach:` pseudo-header that mail_send_current converts into a
     * real multipart MIME part at send time. */
    char **att_paths = mail_extract_attachments_to_tmp(m);
    int att_count = (int)arrlen(att_paths);

    char subj[600];
    fwd_subject(orig_subj, subj, sizeof(subj));

    /* Compose layout:
     *   From / To / Cc / Subject
     *   Attach: ... (one per file)
     *   <blank>
     *   ---------- Forwarded message ----------
     *   From / Date / Subject / To / Cc (original)
     *   <blank>
     *   <body lines from src> */
    char **lines = NULL;
    pushf(&lines, "From: %s", from_addr);
    pushf(&lines, "To: ");
    pushf(&lines, "Cc: ");
    pushf(&lines, "Subject: %s", subj);
    for (int i = 0; i < att_count; i++)
        pushf(&lines, "Attach: %s", att_paths[i]);
    arrput(lines, strdup(""));
    pushf(&lines, "---------- Forwarded message ----------");
    if (orig_from[0])
        pushf(&lines, "From: %s", orig_from);
    if (orig_date[0])
        pushf(&lines, "Date: %s", orig_date);
    if (orig_subj[0])
        pushf(&lines, "Subject: %s", orig_subj);
    if (orig_to[0])
        pushf(&lines, "To: %s", orig_to);
    if (orig_cc[0])
        pushf(&lines, "Cc: %s", orig_cc);
    arrput(lines, strdup(""));
    if (body_start >= 0) {
        for (int i = body_start; i < body_end; i++) {
            const StrBuf *s = &src->rows[i].chars;
            char *dup = malloc(s->len + 1);
            if (!dup)
                continue;
            if (s->len)
                memcpy(dup, s->data, s->len);
            dup[s->len] = '\0';
            arrput(lines, dup);
        }
    }

    mail_compose_with_lines("Forward", lines, (int)arrlen(lines));
    free_lines(lines);
    free_lines(att_paths);

    ed_set_status_message("mail-forward: message %d/%d, %d attachment(s) — "
                          "edit To: and body, C-c C-c or :mail-send to send",
                          mail_msg_number(midx, mcount), mcount, att_count);
}

void mail_forward_eml(void) {
    int midx = 0, mcount = 0;
    const MailMsgSpan *m = forward_source("mail-forward-eml", &midx, &mcount);
    if (!m)
        return;

    const char *orig_subj = m->subject;

    char *eml = mail_extract_raw_to_tmp(m, orig_subj);
    if (!eml) {
        ed_set_status_message("mail-forward-eml: hml show produced no output");
        return;
    }

    char subj[600];
    fwd_subject(orig_subj, subj, sizeof(subj));

    /* The original rides along whole as a message/rfc822 part (see
     * write_multipart); the body is yours to write. */
    char **lines = NULL;
    pushf(&lines, "From: %s", from_addr);
    pushf(&lines, "To: ");
    pushf(&lines, "Cc: ");
    pushf(&lines, "Subject: %s", subj);
    pushf(&lines, "Attach: %s", eml);
    arrput(lines, strdup(""));
    arrput(lines, strdup(""));

    mail_compose_with_lines("Forward", lines, (int)arrlen(lines));
    free_lines(lines);
    free(eml);

    ed_set_status_message("mail-forward-eml: message %d/%d attached as .eml — "
                          "edit To: and body, C-c C-c or :mail-send to send",
                          mail_msg_number(midx, mcount), mcount);
}
