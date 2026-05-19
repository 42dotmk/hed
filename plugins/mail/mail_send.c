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
#include <unistd.h>

/* Highlighter shared with mail_impl.c — same header/quote shape. */
extern size_t mail_msg_hl(Buffer *buf, int row,
                          char *dst, size_t dst_cap,
                          int col_off, int max_cols);

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
    buf->hl_line_fn = mail_msg_hl;

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

/* Concatenate every row of `buf` into one heap-allocated buffer,
 * separated by '\n'.  Caller must free.  Returns NULL on OOM. */
static char *buf_to_string(Buffer *buf, size_t *out_len) {
    size_t total = 0;
    for (int i = 0; i < buf->num_rows; i++)
        total += buf->rows[i].chars.len + 1;
    if (total == 0) total = 1;

    char *out = malloc(total + 1);
    if (!out) return NULL;

    size_t off = 0;
    for (int i = 0; i < buf->num_rows; i++) {
        SizedStr *s = &buf->rows[i].chars;
        if (s->len) {
            memcpy(out + off, s->data, s->len);
            off += s->len;
        }
        out[off++] = '\n';
    }
    out[off] = '\0';
    if (out_len) *out_len = off;
    return out;
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
    size_t msg_len = 0;
    char  *msg     = buf_to_string(buf, &msg_len);
    if (!msg) {
        ed_set_status_message("mail-send: out of memory");
        return;
    }

    /* Write the message to a temp file and feed it via shell
     * redirection.  popen("w") would steal stdin from the child, so
     * msmtp couldn't open the tty to prompt for a password.  Going
     * through term_cmd_system drops raw mode and lets the child
     * inherit the controlling terminal for interactive prompts. */
    char tmpl[] = "/tmp/hed-mail-XXXXXX";
    int  fd     = mkstemp(tmpl);
    if (fd < 0) {
        free(msg);
        ed_set_status_message("mail-send: mkstemp failed");
        return;
    }
    size_t wrote = 0;
    while (wrote < msg_len) {
        ssize_t n = write(fd, msg + wrote, msg_len - wrote);
        if (n <= 0) break;
        wrote += (size_t)n;
    }
    close(fd);
    free(msg);
    if (wrote != msg_len) {
        unlink(tmpl);
        ed_set_status_message("mail-send: short write");
        return;
    }

    char shell_cmd[512];
    snprintf(shell_cmd, sizeof(shell_cmd), "%s < %s", send_cmd, tmpl);
    int rc = term_cmd_system(shell_cmd);
    unlink(tmpl);
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
    buf->hl_line_fn = mail_msg_hl;

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

    char orig_subj[512] = "";
    header_value(src, "Subject", orig_subj, sizeof(orig_subj));

    /* Pull the raw original via notmuch show --format=raw. */
    char qq[256];
    snprintf(qq, sizeof(qq), "'%s'", tid);
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "notmuch show --format=raw -- %s 2>/dev/null", qq);

    char **raw_lines = NULL;
    int    raw_count = 0;
    term_cmd_capture(cmd, &raw_lines, &raw_count);

    /* Build the compose: headers, blank line, separator, raw original. */
    char **lines = NULL;
    int    cap   = raw_count + 16;
    int    n     = 0;
    lines = calloc((size_t)cap, sizeof(*lines));
    if (!lines) {
        term_cmd_free(raw_lines, raw_count);
        ed_set_status_message("mail-forward: out of memory");
        return;
    }

    char from_line[320];
    snprintf(from_line, sizeof(from_line), "From: %s", from_addr);
    char subj_line[576];
    /* Don't double-prefix if the user already has "Fwd: " somewhere. */
    int already_fwd = (strncasecmp(orig_subj, "Fwd:", 4) == 0 ||
                       strncasecmp(orig_subj, "Fw:",  3) == 0);
    snprintf(subj_line, sizeof(subj_line),
             "Subject: %s%s", already_fwd ? "" : "Fwd: ", orig_subj);

    lines[n++] = strdup(from_line);
    lines[n++] = strdup("To: ");
    lines[n++] = strdup("Cc: ");
    lines[n++] = strdup(subj_line);
    lines[n++] = strdup("");
    lines[n++] = strdup("---------- Forwarded message ----------");
    for (int i = 0; i < raw_count && n < cap; i++)
        lines[n++] = raw_lines[i] ? strdup(raw_lines[i]) : strdup("");

    compose_from_lines("Forward", lines, n);
    for (int i = 0; i < n; i++) free(lines[i]);
    free(lines);
    term_cmd_free(raw_lines, raw_count);
    ed_set_status_message("mail-forward: edit To: and body, :mail-send to send");
}
