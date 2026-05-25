/* mail_send: compose + send outgoing mail.
 *
 * `mail_compose()` opens a fresh editable buffer pre-filled with the
 * standard headers and lands in insert mode at the To: line.
 * `mail_send_current()` pipes the active buffer to the configured
 * send command (default `msmtp -t -a default`) which is expected to
 * read an RFC 822 message on stdin and route it from the To/Cc/Bcc
 * headers. */

#include "mail.h"
#include "hed.h"
#include "buf/row.h"
#include "utils/term_cmd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* Internal row helper exposed by buf/buffer.c. */
void buf_row_insert_in(Buffer *buf, int at, const char *s, size_t len);

static char send_cmd[256] = "msmtp -t";
static char from_addr[256] = "";
static int  compose_seq    = 0;

void mail_set_send_cmd(const char *cmd) {
    snprintf(send_cmd, sizeof(send_cmd), "%s",
             (cmd && *cmd) ? cmd : "msmtp -t");
}

void mail_set_from(const char *from) {
    snprintf(from_addr, sizeof(from_addr), "%s", from ? from : "");
}

const char *mail_get_from(void) {
    return from_addr;
}

static void insert_line(Buffer *buf, const char *s) {
    buf_row_insert_in(buf, buf->num_rows, s, strlen(s));
}

void mail_compose(void) {
    char bufname[64];
    snprintf(bufname, sizeof(bufname), "mail://compose-%d", ++compose_seq);

    int idx = -1;
    if (buf_new(bufname, &idx) != ED_OK) {
        ed_set_status_message("mail: failed to open compose buffer");
        return;
    }

    Buffer *buf = &E.buffers[idx];
    free(buf->title);    buf->title    = strdup("Compose");
    free(buf->filetype); buf->filetype = strdup("mail-compose");
    buf->readonly   = 0;
    /* Highlighting via mail_msg_render_hook (registered for filetypes
     * mail-message and mail-compose). */

    char from_line[320];
    snprintf(from_line, sizeof(from_line), "From: %s", from_addr);
    insert_line(buf, from_line);
    insert_line(buf, "To: ");
    insert_line(buf, "Cc: ");
    insert_line(buf, "Subject: ");
    insert_line(buf, "");
    insert_line(buf, "");
    buf->dirty = 0;

    Window *win = window_cur();
    if (win) {
        win_attach_buf(win, buf);
        /* Land on the To: line, after the prefix. */
        win->cursor.y = 1;
        win->cursor.x = 4;
    }
    E.current_buffer = idx;

    ed_set_mode(MODE_INSERT);
    ed_set_status_message(
        "mail: compose — edit headers + body, then :mail-send");
}

/* Locate the header named `name` (case-insensitive, no colon) and
 * report whether it contains any non-whitespace value.  Headers stop
 * at the first blank line. */
static int header_has_value(Buffer *buf, const char *name) {
    size_t nlen = strlen(name);
    for (int i = 0; i < buf->num_rows; i++) {
        SizedStr *s = &buf->rows[i].chars;
        if (s->len == 0) return 0; /* end of headers */
        if (s->len <= nlen + 1) continue;
        if (strncasecmp(s->data, name, nlen) != 0) continue;
        if (s->data[nlen] != ':') continue;
        for (size_t k = nlen + 1; k < s->len; k++) {
            char c = s->data[k];
            if (c != ' ' && c != '\t') return 1;
        }
        return 0;
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
                          (got > 2 ?  (unsigned long)buf[2]      : 0);
        char enc[4] = {
            b64chars[(v >> 18) & 0x3f],
            b64chars[(v >> 12) & 0x3f],
            got > 1 ? b64chars[(v >> 6) & 0x3f] : '=',
            got > 2 ? b64chars[v & 0x3f]        : '=',
        };
        if (fwrite(enc, 1, 4, out) != 4) return 1;
        col += 4;
        if (col >= 76) { fputc('\n', out); col = 0; }
    }
    if (col > 0) fputc('\n', out);
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
    /* Tiny inline shell-quote for the path. */
    size_t pl = 0;
    pq[pl++] = '\'';
    for (const char *p = path; *p && pl + 5 < sizeof(pq); p++) {
        if (*p == '\'') { pq[pl++] = '\''; pq[pl++] = '\\';
                          pq[pl++] = '\''; pq[pl++] = '\''; }
        else pq[pl++] = *p;
    }
    pq[pl++] = '\''; pq[pl] = '\0';
    char cmd[1400];
    snprintf(cmd, sizeof(cmd), "file --mime-type -b -- %s 2>/dev/null", pq);
    FILE *fp = popen(cmd, "r");
    if (!fp) return;
    char tmp[256];
    if (fgets(tmp, sizeof(tmp), fp)) {
        size_t L = strlen(tmp);
        while (L > 0 && (tmp[L-1] == '\n' || tmp[L-1] == '\r' ||
                          tmp[L-1] == ' '  || tmp[L-1] == '\t'))
            tmp[--L] = '\0';
        if (L > 0 && strchr(tmp, '/')) {
            /* Truncate if the sniffed value won't fit; the caller's
             * `out` is sized for "type/subtype" plus a small margin. */
            if (L >= cap) L = cap - 1;
            memcpy(out, tmp, L);
            out[L] = '\0';
        }
    }
    pclose(fp);
}

/* Walk the header block of buf, collecting Attach: values into a fresh
 * paths[] array. Sets *out_body_start to the row index just past the
 * blank line that ends the header block. Returns number of paths. */
static int collect_attach_paths(Buffer *buf, char ***out_paths,
                                int *out_body_start) {
    if (out_paths) *out_paths = NULL;
    if (out_body_start) *out_body_start = -1;
    char **paths = NULL;
    int    cnt = 0, cap = 0;
    int    i;
    for (i = 0; i < buf->num_rows; i++) {
        SizedStr *s = &buf->rows[i].chars;
        if (s->len == 0) { i++; break; }
        if (s->len > 7 && strncasecmp(s->data, "Attach:", 7) == 0) {
            size_t k = 7;
            while (k < s->len && (s->data[k] == ' ' || s->data[k] == '\t')) k++;
            size_t vlen = s->len - k;
            if (vlen == 0) continue;
            if (cnt >= cap) {
                int ncap = cap ? cap * 2 : 4;
                char **np = realloc(paths, (size_t)ncap * sizeof(*paths));
                if (!np) break;
                paths = np;
                cap = ncap;
            }
            char *p = malloc(vlen + 1);
            if (!p) break;
            memcpy(p, s->data + k, vlen);
            p[vlen] = '\0';
            paths[cnt++] = p;
        }
    }
    if (out_body_start) *out_body_start = i;
    if (out_paths) *out_paths = paths;
    return cnt;
}

/* Write the headers of buf to fp, suppressing any Attach: lines, and
 * inserting MIME-Version + Content-Type:multipart/mixed when
 * boundary != NULL. Stops at the blank line that ends the header
 * block (which is also written). */
static int write_headers_with_mime(Buffer *buf, FILE *fp,
                                   const char *boundary) {
    int wrote_mime = 0;
    for (int i = 0; i < buf->num_rows; i++) {
        SizedStr *s = &buf->rows[i].chars;
        /* Skip Attach: pseudo-headers — they're consumed into the
         * MIME envelope, not emitted to the wire. */
        if (s->len > 7 && strncasecmp(s->data, "Attach:", 7) == 0)
            continue;
        if (boundary && !wrote_mime && s->len == 0) {
            fprintf(fp, "MIME-Version: 1.0\r\n");
            fprintf(fp, "Content-Type: multipart/mixed; boundary=\"%s\"\r\n",
                    boundary);
            wrote_mime = 1;
        }
        if (s->len) fwrite(s->data, 1, s->len, fp);
        fputs("\r\n", fp);
        if (s->len == 0) return i + 1; /* body starts on next row */
    }
    return buf->num_rows;
}

/* Write a multipart-MIME version of buf to fp. Returns 0 on success. */
static int write_multipart(Buffer *buf, FILE *fp,
                           char **att_paths, int att_count,
                           const char *boundary) {
    int body_start = write_headers_with_mime(buf, fp, boundary);

    /* Text body part. */
    fprintf(fp, "--%s\r\n", boundary);
    fprintf(fp, "Content-Type: text/plain; charset=utf-8\r\n");
    fprintf(fp, "Content-Transfer-Encoding: 8bit\r\n\r\n");
    for (int i = body_start; i < buf->num_rows; i++) {
        SizedStr *s = &buf->rows[i].chars;
        if (s->len) fwrite(s->data, 1, s->len, fp);
        fputs("\r\n", fp);
    }

    /* One part per attachment. */
    for (int i = 0; i < att_count; i++) {
        const char *path = att_paths[i];
        FILE *af = fopen(path, "rb");
        if (!af) return 1;
        char mime[128];
        sniff_mime_type(path, mime, sizeof(mime));
        const char *base = path_basename(path);
        fprintf(fp, "--%s\r\n", boundary);
        fprintf(fp, "Content-Type: %s; name=\"%s\"\r\n", mime, base);
        fprintf(fp, "Content-Disposition: attachment; filename=\"%s\"\r\n", base);
        fprintf(fp, "Content-Transfer-Encoding: base64\r\n\r\n");
        int rc = base64_stream(af, fp);
        fclose(af);
        if (rc) return rc;
    }

    fprintf(fp, "--%s--\r\n", boundary);
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
    if (!header_has_value(buf, "To")) {
        ed_set_status_message("mail-send: missing To: header");
        return;
    }
    if (!header_has_value(buf, "Subject")) {
        ed_set_status_message("mail-send: missing Subject: header");
        return;
    }

    /* Collect Attach: pseudo-headers. If any are present we emit a
     * multipart/mixed envelope; otherwise the message goes out as
     * plain text exactly as before. */
    char **att_paths = NULL;
    int    att_count = collect_attach_paths(buf, &att_paths, NULL);

    char tmpl[PATH_MAX];
    if (fs_temp_path("hed-mail", tmpl, sizeof(tmpl)) != ED_OK) {
        for (int i = 0; i < att_count; i++) free(att_paths[i]);
        free(att_paths);
        ed_set_status_message("mail-send: failed to reserve temp file");
        return;
    }
    FILE *fp = fopen(tmpl, "w");
    if (!fp) {
        fs_unlink(tmpl);
        for (int i = 0; i < att_count; i++) free(att_paths[i]);
        free(att_paths);
        ed_set_status_message("mail-send: failed to open temp file");
        return;
    }

    int wr_err = 0;
    if (att_count > 0) {
        char boundary[64];
        snprintf(boundary, sizeof(boundary), "=_hed_%d_%ld",
                 (int)getpid(), (long)time(NULL));
        wr_err = write_multipart(buf, fp, att_paths, att_count, boundary);
    } else {
        for (int i = 0; i < buf->num_rows; i++) {
            SizedStr *s = &buf->rows[i].chars;
            if (s->len) fwrite(s->data, 1, s->len, fp);
            fputc('\n', fp);
        }
    }
    if (fflush(fp) != 0 || ferror(fp)) wr_err = 1;
    fclose(fp);
    if (wr_err) {
        fs_unlink(tmpl);
        for (int i = 0; i < att_count; i++) free(att_paths[i]);
        free(att_paths);
        ed_set_status_message("mail-send: failed to write message");
        return;
    }

    char shell_cmd[PATH_MAX + 512];
    snprintf(shell_cmd, sizeof(shell_cmd), "%s < %s", send_cmd, tmpl);
    int rc = term_cmd_system(shell_cmd);
    fs_unlink(tmpl);
    if (rc != 0) {
        if (WIFEXITED(rc))
            ed_set_status_message("mail-send: %s exited %d",
                                  send_cmd, WEXITSTATUS(rc));
        else if (WIFSIGNALED(rc))
            ed_set_status_message("mail-send: %s killed by signal %d",
                                  send_cmd, WTERMSIG(rc));
        else
            ed_set_status_message("mail-send: %s failed (status %d)",
                                  send_cmd, rc);
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
    if (!buf || !buf->filename) return NULL;
    if (!buf->filetype || strcmp(buf->filetype, "mail-message") != 0) return NULL;
    const char *fn = buf->filename;
    if (strncmp(fn, "mail://", 7) != 0) return NULL;
    return fn + 7;
}

/* Find the first header line "<Name>: <value>" in buf and copy the
 * trimmed value into out. Returns 1 on hit. */
static int header_value(Buffer *buf, const char *name, char *out, size_t cap) {
    size_t nlen = strlen(name);
    for (int i = 0; i < buf->num_rows; i++) {
        SizedStr *s = &buf->rows[i].chars;
        if (s->len == 0) return 0; /* end of headers */
        if (s->len <= nlen + 1) continue;
        if (strncasecmp(s->data, name, nlen) != 0) continue;
        if (s->data[nlen] != ':') continue;
        size_t k = nlen + 1;
        while (k < s->len && (s->data[k] == ' ' || s->data[k] == '\t')) k++;
        size_t vlen = s->len - k;
        if (vlen >= cap) vlen = cap - 1;
        memcpy(out, s->data + k, vlen);
        out[vlen] = '\0';
        return 1;
    }
    return 0;
}

/* Create a fresh compose buffer, fill from lines, attach to the
 * current window, set insert mode at body. */
static void compose_from_lines(const char *title, char **lines, int count) {
    char bufname[64];
    snprintf(bufname, sizeof(bufname), "mail://compose-%d", ++compose_seq);

    int idx = -1;
    if (buf_new(bufname, &idx) != ED_OK) {
        ed_set_status_message("mail: failed to open compose buffer");
        return;
    }

    Buffer *buf = &E.buffers[idx];
    free(buf->title);    buf->title    = strdup(title);
    free(buf->filetype); buf->filetype = strdup("mail-compose");
    buf->readonly   = 0;
    /* Highlighting via mail_msg_render_hook (registered for filetypes
     * mail-message and mail-compose). */

    int body_row = -1;
    for (int i = 0; i < count; i++) {
        const char *s = lines[i] ? lines[i] : "";
        buf_row_insert_in(buf, buf->num_rows, s, strlen(s));
        if (body_row < 0 && s[0] == '\0') body_row = i + 1;
    }
    if (body_row < 0) body_row = buf->num_rows;
    buf->dirty = 0;

    Window *win = window_cur();
    if (win) {
        win_attach_buf(win, buf);
        win->cursor.y = body_row;
        win->cursor.x = 0;
    }
    E.current_buffer = idx;
    ed_set_mode(MODE_INSERT);
}

void mail_compose_with_lines(const char *title, char **lines, int count) {
    compose_from_lines(title, lines, count);
}

/* ------------------------------------------------------------------ */
/* mailto: URI (RFC 6068) → compose                                    */
/* ------------------------------------------------------------------ */

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Percent-decode `src` (length `len`) into a fresh malloc'd NUL-terminated
 * string. `form` non-zero also decodes '+' as space (tolerant — RFC 6068
 * mandates %20, but many producers still emit '+'). Returns NULL on OOM. */
static char *url_decode(const char *src, size_t len, int form) {
    char *out = malloc(len + 1);
    if (!out) return NULL;
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
        if (form && c == '+') { out[j++] = ' '; continue; }
        out[j++] = c;
    }
    out[j] = '\0';
    return out;
}

/* Append `add` to the comma-separated list in `*dst` (malloc'd). Skips
 * empty additions. Frees and replaces `*dst`. */
static void append_csv(char **dst, const char *add) {
    if (!add || !*add) return;
    if (!*dst || !**dst) {
        free(*dst);
        *dst = strdup(add);
        return;
    }
    size_t need = strlen(*dst) + 2 + strlen(add) + 1;
    char *nw = malloc(need);
    if (!nw) return;
    snprintf(nw, need, "%s, %s", *dst, add);
    free(*dst);
    *dst = nw;
}

/* Push one `Header: value` line onto a growing array. */
static void push_header(char ***arr, int *count, int *cap,
                        const char *name, const char *val) {
    if (!val) return;
    size_t need = strlen(name) + 2 + strlen(val) + 1;
    char *line = malloc(need);
    if (!line) return;
    snprintf(line, need, "%s: %s", name, val);

    if (*count >= *cap) {
        int nc = *cap ? *cap * 2 : 16;
        char **na = realloc(*arr, (size_t)nc * sizeof(char *));
        if (!na) { free(line); return; }
        *arr = na;
        *cap = nc;
    }
    (*arr)[(*count)++] = line;
}

static void push_raw(char ***arr, int *count, int *cap, const char *s) {
    if (*count >= *cap) {
        int nc = *cap ? *cap * 2 : 16;
        char **na = realloc(*arr, (size_t)nc * sizeof(char *));
        if (!na) return;
        *arr = na;
        *cap = nc;
    }
    (*arr)[(*count)++] = strdup(s ? s : "");
}

void mail_compose_uri(const char *uri) {
    if (!uri) return;
    if (strncmp(uri, "mailto:", 7) != 0) {
        ed_set_status_message("mail: not a mailto: URI");
        return;
    }

    const char *rest = uri + 7;
    const char *q    = strchr(rest, '?');
    size_t      path_len = q ? (size_t)(q - rest) : strlen(rest);

    char *to_acc      = NULL;
    char *cc_acc      = NULL;
    char *bcc_acc     = NULL;
    char *subject     = NULL;
    char *body        = NULL;
    char *in_reply_to = NULL;
    char *references  = NULL;

    /* Path: comma-list of recipients. Each recipient is percent-decoded
     * individually so commas inside encoded display-names (%2C) survive. */
    if (path_len > 0) {
        size_t start = 0;
        for (size_t i = 0; i <= path_len; i++) {
            if (i == path_len || rest[i] == ',') {
                char *dec = url_decode(rest + start, i - start, 0);
                if (dec && *dec) append_csv(&to_acc, dec);
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
            size_t      seg = amp ? (size_t)(amp - p) : strlen(p);
            const char *eq  = memchr(p, '=', seg);
            if (eq) {
                size_t klen = (size_t)(eq - p);
                size_t vlen = seg - klen - 1;
                char  *key  = url_decode(p, klen, 0);
                char  *val  = url_decode(eq + 1, vlen, 1);
                if (key && val) {
                    if      (!strcasecmp(key, "to"))          append_csv(&to_acc,  val);
                    else if (!strcasecmp(key, "cc"))          append_csv(&cc_acc,  val);
                    else if (!strcasecmp(key, "bcc"))         append_csv(&bcc_acc, val);
                    else if (!strcasecmp(key, "subject"))     { free(subject);     subject     = strdup(val); }
                    else if (!strcasecmp(key, "body"))        { free(body);        body        = strdup(val); }
                    else if (!strcasecmp(key, "in-reply-to")) { free(in_reply_to); in_reply_to = strdup(val); }
                    else if (!strcasecmp(key, "references"))  { free(references);  references  = strdup(val); }
                    /* unknown keys: ignore per RFC 6068 §4 */
                }
                free(key); free(val);
            }
            p = amp ? amp + 1 : p + seg;
        }
    }

    /* Build the line array. */
    char **lines = NULL;
    int    count = 0, cap = 0;

    push_header(&lines, &count, &cap, "From",    from_addr[0] ? from_addr : "");
    push_header(&lines, &count, &cap, "To",      to_acc  ? to_acc  : "");
    push_header(&lines, &count, &cap, "Cc",      cc_acc  ? cc_acc  : "");
    if (bcc_acc && *bcc_acc)
        push_header(&lines, &count, &cap, "Bcc", bcc_acc);
    push_header(&lines, &count, &cap, "Subject", subject ? subject : "");
    if (in_reply_to && *in_reply_to)
        push_header(&lines, &count, &cap, "In-Reply-To", in_reply_to);
    if (references && *references)
        push_header(&lines, &count, &cap, "References", references);

    /* Header/body separator. */
    push_raw(&lines, &count, &cap, "");

    /* Body: split on CR/LF in either order. */
    if (body && *body) {
        char  *p   = body;
        char  *seg = body;
        while (*p) {
            if (*p == '\n' || *p == '\r') {
                char save = *p;
                *p = '\0';
                push_raw(&lines, &count, &cap, seg);
                if (save == '\r' && p[1] == '\n') p++;
                p++;
                seg = p;
            } else {
                p++;
            }
        }
        if (*seg) push_raw(&lines, &count, &cap, seg);
    } else {
        push_raw(&lines, &count, &cap, "");
    }

    compose_from_lines("Compose (mailto)", lines, count);

    for (int i = 0; i < count; i++) free(lines[i]);
    free(lines);
    free(to_acc); free(cc_acc); free(bcc_acc);
    free(subject); free(body);
    free(in_reply_to); free(references);

    ed_set_status_message("mail: compose from mailto: URI");
}

void mail_reply(int reply_all) {
    Buffer *src = buf_cur();
    const char *tid = current_thread_id(src);
    if (!tid) {
        ed_set_status_message("mail-reply: open a message first");
        return;
    }

    char qq[256];
    snprintf(qq, sizeof(qq), "'%s'", tid);
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "notmuch reply --reply-to=%s -- %s 2>/dev/null",
             reply_all ? "all" : "sender", qq);

    char **lines = NULL;
    int    count = 0;
    term_cmd_capture(cmd, &lines, &count);
    if (count == 0) {
        ed_set_status_message("mail-reply: notmuch reply produced no output");
        term_cmd_free(lines, count);
        return;
    }

    /* If a from_addr is configured, override notmuch's From: line. */
    if (from_addr[0]) {
        for (int i = 0; i < count; i++) {
            if (lines[i] && strncasecmp(lines[i], "From:", 5) == 0) {
                free(lines[i]);
                size_t need = 6 + strlen(from_addr) + 1;
                lines[i] = malloc(need);
                if (lines[i]) snprintf(lines[i], need, "From: %s", from_addr);
                break;
            }
        }
    }

    compose_from_lines(reply_all ? "Reply-All" : "Reply", lines, count);
    term_cmd_free(lines, count);
    ed_set_status_message(
        "mail-reply: %s — edit body, :mail-send to send",
        reply_all ? "reply-all" : "sender");
}

void mail_forward(void) {
    Buffer *src = buf_cur();
    const char *tid = current_thread_id(src);
    if (!tid) {
        ed_set_status_message("mail-forward: open a message first");
        return;
    }

    /* Pull the original headers (From / Date / Subject / To / Cc)
     * directly from the rendered message buffer — same source the
     * user is reading. */
    char orig_from[512] = "", orig_date[256] = "", orig_subj[512] = "";
    char orig_to[512]   = "", orig_cc[512]   = "";
    header_value(src, "From",    orig_from, sizeof(orig_from));
    header_value(src, "Date",    orig_date, sizeof(orig_date));
    header_value(src, "Subject", orig_subj, sizeof(orig_subj));
    header_value(src, "To",      orig_to,   sizeof(orig_to));
    header_value(src, "Cc",      orig_cc,   sizeof(orig_cc));

    /* Find the body of the first message in the buffer: everything
     * after the first blank line (which separates headers from body)
     * and before either EOF or the section divider mail_parse inserts
     * between messages in a thread. */
    int body_start = -1;
    for (int i = 0; i < src->num_rows; i++) {
        if (src->rows[i].chars.len == 0) { body_start = i + 1; break; }
    }
    int body_end = src->num_rows;
    if (body_start >= 0) {
        for (int i = body_start; i < src->num_rows; i++) {
            const SizedStr *s = &src->rows[i].chars;
            /* mail_parse uses a long "─" run as the per-message divider. */
            if (s->len >= 3 && (unsigned char)s->data[0] == 0xE2 &&
                (unsigned char)s->data[1] == 0x94 &&
                (unsigned char)s->data[2] == 0x80) {
                body_end = i;
                break;
            }
        }
    }

    /* Extract any attachments into /tmp; each becomes an `Attach:`
     * pseudo-header that mail_send_current converts into a real
     * multipart MIME part at send time. */
    char **att_paths = NULL;
    int    att_count = mail_extract_attachments_to_tmp(&att_paths);

    char from_line[320];
    snprintf(from_line, sizeof(from_line), "From: %s", from_addr);

    /* Don't double-prefix "Fwd: " if the source subject already has it. */
    int already_fwd = (strncasecmp(orig_subj, "Fwd:", 4) == 0 ||
                       strncasecmp(orig_subj, "Fw:",  3) == 0);
    char subj_line[576];
    snprintf(subj_line, sizeof(subj_line),
             "Subject: %s%s", already_fwd ? "" : "Fwd: ", orig_subj);

    /* Compose layout:
     *   From / To / Cc / Subject
     *   Attach: ... (one per file)
     *   <blank>
     *   ---------- Forwarded message ----------
     *   From / Date / Subject / To / Cc (original)
     *   <blank>
     *   <body lines from src> */
    int body_lines = (body_start >= 0) ? (body_end - body_start) : 0;
    if (body_lines < 0) body_lines = 0;
    int cap = 4 + att_count + 2 + 5 + 1 + body_lines + 4;
    char **lines = calloc((size_t)cap, sizeof(*lines));
    if (!lines) {
        for (int i = 0; i < att_count; i++) free(att_paths[i]);
        free(att_paths);
        ed_set_status_message("mail-forward: out of memory");
        return;
    }
    int n = 0;
    lines[n++] = strdup(from_line);
    lines[n++] = strdup("To: ");
    lines[n++] = strdup("Cc: ");
    lines[n++] = strdup(subj_line);
    for (int i = 0; i < att_count; i++) {
        char ab[1280];
        snprintf(ab, sizeof(ab), "Attach: %s", att_paths[i]);
        lines[n++] = strdup(ab);
    }
    lines[n++] = strdup("");
    lines[n++] = strdup("---------- Forwarded message ----------");
    if (orig_from[0]) {
        char b[576]; snprintf(b, sizeof(b), "From: %s", orig_from);
        lines[n++] = strdup(b);
    }
    if (orig_date[0]) {
        char b[320]; snprintf(b, sizeof(b), "Date: %s", orig_date);
        lines[n++] = strdup(b);
    }
    if (orig_subj[0]) {
        char b[576]; snprintf(b, sizeof(b), "Subject: %s", orig_subj);
        lines[n++] = strdup(b);
    }
    if (orig_to[0]) {
        char b[576]; snprintf(b, sizeof(b), "To: %s", orig_to);
        lines[n++] = strdup(b);
    }
    if (orig_cc[0]) {
        char b[576]; snprintf(b, sizeof(b), "Cc: %s", orig_cc);
        lines[n++] = strdup(b);
    }
    lines[n++] = strdup("");
    for (int i = 0; i < body_lines && n < cap; i++) {
        const SizedStr *s = &src->rows[body_start + i].chars;
        char *dup = malloc(s->len + 1);
        if (dup) {
            if (s->len) memcpy(dup, s->data, s->len);
            dup[s->len] = '\0';
        }
        lines[n++] = dup ? dup : strdup("");
    }

    compose_from_lines("Forward", lines, n);
    for (int i = 0; i < n; i++) free(lines[i]);
    free(lines);
    for (int i = 0; i < att_count; i++) free(att_paths[i]);
    free(att_paths);

    if (att_count > 0)
        ed_set_status_message(
            "mail-forward: edit To: and body — %d attachment(s) cached, "
            ":mail-send to send",
            att_count);
    else
        ed_set_status_message(
            "mail-forward: edit To: and body, :mail-send to send");
}
