#include "mail_parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/wait.h>

#define PART_DEPTH_MAX 8

static void lines_push(MailRender *r, const char *s, size_t len) {
    if (r->line_count == r->line_cap) {
        int ncap = r->line_cap ? r->line_cap * 2 : 64;
        char **nl = realloc(r->lines, (size_t)ncap * sizeof(*nl));
        if (!nl) return;
        r->lines   = nl;
        r->line_cap = ncap;
    }
    char *dup = malloc(len + 1);
    if (!dup) return;
    if (len) memcpy(dup, s, len);
    dup[len] = '\0';
    r->lines[r->line_count++] = dup;
}

static void lines_pushz(MailRender *r, const char *s) {
    lines_push(r, s, strlen(s));
}

static void attach_push(MailRender *r, const MailAttachInfo *a) {
    if (r->attach_count == r->attach_cap) {
        int ncap = r->attach_cap ? r->attach_cap * 2 : 8;
        MailAttachInfo *na = realloc(r->attaches, (size_t)ncap * sizeof(*na));
        if (!na) return;
        r->attaches   = na;
        r->attach_cap = ncap;
    }
    r->attaches[r->attach_count++] = *a;
}

void mail_render_init(MailRender *r) {
    memset(r, 0, sizeof(*r));
}

void mail_render_free(MailRender *r) {
    for (int i = 0; i < r->line_count; i++) free(r->lines[i]);
    free(r->lines);
    free(r->attaches);
    memset(r, 0, sizeof(*r));
}

/* Parse `key: value` out of one of the inline notmuch marker lines.
 * `comma_sep` controls termination:
 *   1 — comma-separated (\fpart{, \fattachment{): value runs until ", "
 *   0 — space-separated (\fmessage{): value runs until next space */
static int marker_field(const char *line, const char *key,
                        char *out, size_t cap, int comma_sep) {
    const char *p = strstr(line, key);
    if (!p) return 0;
    p += strlen(key);
    while (*p == ' ' || *p == '\t') p++;
    size_t n = 0;
    while (*p && n + 1 < cap) {
        if (comma_sep) {
            if (p[0] == ',' && p[1] == ' ') break;
        } else {
            if (*p == ' ' || *p == '\t') break;
        }
        out[n++] = *p++;
    }
    while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\t')) n--;
    out[n] = '\0';
    return 1;
}

static int starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static int header_match(const char *line, const char *name,
                        const char **value) {
    size_t nlen = strlen(name);
    if (strncasecmp(line, name, nlen) != 0) return 0;
    if (line[nlen] != ':') return 0;
    const char *p = line + nlen + 1;
    while (*p == ' ' || *p == '\t') p++;
    *value = p;
    return 1;
}

/* Run `w3m -dump -T text/html` over `html`, splitting stdout into lines
 * appended to `out`. Falls back to `lynx -dump -stdin` if w3m is missing.
 * On total failure, emits a single placeholder line. */
static void render_html(const char *html, size_t len, MailRender *out) {
    const char *cmds[] = {
        "w3m -dump -T text/html -o display_link_number=false 2>/dev/null",
        "lynx -dump -stdin -nolist 2>/dev/null",
        NULL
    };
    for (int i = 0; cmds[i]; i++) {
        int in_pipe[2], out_pipe[2];
        if (pipe(in_pipe) != 0) continue;
        if (pipe(out_pipe) != 0) { close(in_pipe[0]); close(in_pipe[1]); continue; }

        pid_t pid = fork();
        if (pid < 0) {
            close(in_pipe[0]); close(in_pipe[1]);
            close(out_pipe[0]); close(out_pipe[1]);
            continue;
        }
        if (pid == 0) {
            dup2(in_pipe[0], 0);
            dup2(out_pipe[1], 1);
            close(in_pipe[0]); close(in_pipe[1]);
            close(out_pipe[0]); close(out_pipe[1]);
            execl("/bin/sh", "sh", "-c", cmds[i], (char *)NULL);
            _exit(127);
        }
        close(in_pipe[0]);
        close(out_pipe[1]);

        /* Feed html on stdin. Ignore SIGPIPE-on-short-read by checking write. */
        size_t off = 0;
        while (off < len) {
            ssize_t n = write(in_pipe[1], html + off, len - off);
            if (n <= 0) break;
            off += (size_t)n;
        }
        close(in_pipe[1]);

        /* Slurp output. */
        char  *buf  = NULL;
        size_t bcap = 0, blen = 0;
        char   tmp[4096];
        for (;;) {
            ssize_t n = read(out_pipe[0], tmp, sizeof(tmp));
            if (n <= 0) break;
            if (blen + (size_t)n + 1 > bcap) {
                size_t ncap = bcap ? bcap * 2 : 8192;
                while (ncap < blen + (size_t)n + 1) ncap *= 2;
                char *nb = realloc(buf, ncap);
                if (!nb) break;
                buf  = nb;
                bcap = ncap;
            }
            memcpy(buf + blen, tmp, (size_t)n);
            blen += (size_t)n;
        }
        close(out_pipe[0]);

        int status = 0;
        waitpid(pid, &status, 0);

        if (buf && blen > 0 &&
            WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            buf[blen] = '\0';
            size_t a = 0;
            for (size_t b = 0; b <= blen; b++) {
                if (b == blen || buf[b] == '\n') {
                    lines_push(out, buf + a, b - a);
                    a = b + 1;
                }
            }
            free(buf);
            return;
        }
        free(buf);
    }
    lines_pushz(out, "(HTML body — install w3m or lynx to render)");
}

typedef struct {
    char from[512];
    char to[512];
    char cc[512];
    char subject[512];
    char date[256];
    char msg_id[256];
    int  depth;

    /* body text accumulator (plain text/plain content) */
    char  *plain;
    size_t plain_len, plain_cap;
    /* fallback html accumulator */
    char  *html;
    size_t html_len, html_cap;
    int    have_plain;

    /* indices into render->attaches for this message */
    int attach_start;
} MsgState;

static void buf_append(char **buf, size_t *len, size_t *cap,
                       const char *s, size_t n, int add_nl) {
    size_t need = *len + n + (add_nl ? 1 : 0) + 1;
    if (need > *cap) {
        size_t ncap = *cap ? *cap : 1024;
        while (ncap < need) ncap *= 2;
        char *nb = realloc(*buf, ncap);
        if (!nb) return;
        *buf = nb;
        *cap = ncap;
    }
    if (n) memcpy(*buf + *len, s, n);
    *len += n;
    if (add_nl) (*buf)[(*len)++] = '\n';
    (*buf)[*len] = '\0';
}

static void msg_state_reset(MsgState *m) {
    free(m->plain);
    free(m->html);
    memset(m, 0, sizeof(*m));
}

static void emit_msg(MailRender *r, MsgState *m, int is_first) {
    if (!is_first) {
        lines_pushz(r, "");
        lines_pushz(r, "──────────────────────────────────────────");
        lines_pushz(r, "");
    }

    char line[1024];
    if (m->from[0]) {
        snprintf(line, sizeof(line), "From:    %s", m->from);
        lines_pushz(r, line);
    }
    if (m->to[0]) {
        snprintf(line, sizeof(line), "To:      %s", m->to);
        lines_pushz(r, line);
    }
    if (m->cc[0]) {
        snprintf(line, sizeof(line), "Cc:      %s", m->cc);
        lines_pushz(r, line);
    }
    if (m->subject[0]) {
        snprintf(line, sizeof(line), "Subject: %s", m->subject);
        lines_pushz(r, line);
    }
    if (m->date[0]) {
        snprintf(line, sizeof(line), "Date:    %s", m->date);
        lines_pushz(r, line);
    }

    int n_att = r->attach_count - m->attach_start;
    if (n_att > 0) {
        size_t cap = 32 + (size_t)n_att * 80;
        char  *al  = malloc(cap);
        if (al) {
            size_t off = (size_t)snprintf(al, cap, "Attachments:");
            for (int i = 0; i < n_att; i++) {
                const MailAttachInfo *a = &r->attaches[m->attach_start + i];
                off += (size_t)snprintf(al + off, cap - off, "  [%d] %s",
                                        a->part_id,
                                        a->filename[0] ? a->filename : "(unnamed)");
            }
            lines_pushz(r, al);
            free(al);
        }
    }

    lines_pushz(r, "");

    /* Body: prefer plain. Fall back to html via w3m/lynx. */
    char  *body     = NULL;
    size_t body_len = 0;
    if (m->have_plain && m->plain_len > 0) {
        body     = m->plain;
        body_len = m->plain_len;
    }
    if (body) {
        /* Strip leading and trailing blank lines while splitting. */
        size_t a = 0;
        /* Skip leading whitespace-only lines */
        while (a < body_len) {
            size_t b = a;
            while (b < body_len && body[b] != '\n') b++;
            int blank = 1;
            for (size_t k = a; k < b; k++)
                if (body[k] != ' ' && body[k] != '\t') { blank = 0; break; }
            if (!blank) break;
            a = b + 1;
        }
        /* Trim trailing blank lines */
        size_t end = body_len;
        while (end > a) {
            size_t e = end;
            size_t s = e;
            while (s > a && body[s - 1] != '\n') s--;
            int blank = 1;
            for (size_t k = s; k < e - (e > 0 && body[e - 1] == '\n' ? 1 : 0); k++)
                if (body[k] != ' ' && body[k] != '\t') { blank = 0; break; }
            if (!blank) break;
            end = s > 0 ? s - 1 : 0;
            if (s == a) break;
        }
        size_t p = a;
        while (p <= end) {
            size_t q = p;
            while (q < end && body[q] != '\n') q++;
            lines_push(r, body + p, q - p);
            p = q + 1;
        }
    } else if (m->html && m->html_len > 0) {
        render_html(m->html, m->html_len, r);
    } else {
        lines_pushz(r, "(empty body)");
    }
}

void mail_render_notmuch_text(MailRender *r, char **raw, int raw_count) {
    MsgState msg;
    memset(&msg, 0, sizeof(msg));
    int in_message = 0;
    int is_first   = 1;

    /* Header block flag (between \fheader{ and \fheader}). */
    int in_header = 0;

    /* Part stack: each level tracks whether we're capturing into plain/html. */
    int   pstack_depth = 0;
    int   pstack_mode[PART_DEPTH_MAX];   /* 0=skip, 1=plain, 2=html */
    /* multipart suppression: when 1, the wrapper part itself isn't capturing
     * but its children may. We only track via the per-level mode. */

    /* Attachment context: filename/type discovered between \fattachment{
     * and \fattachment}. */
    MailAttachInfo cur_att;
    int             in_attachment = 0;

    for (int i = 0; i < raw_count; i++) {
        const char *line = raw[i] ? raw[i] : "";

        /* --- markers ------------------------------------------------- */
        if (starts_with(line, "\fmessage{")) {
            if (in_message) {
                emit_msg(r, &msg, is_first);
                is_first = 0;
                msg_state_reset(&msg);
            }
            in_message       = 1;
            in_header        = 0;
            pstack_depth     = 0;
            msg.attach_start = r->attach_count;
            marker_field(line, "id:",    msg.msg_id, sizeof(msg.msg_id), 0);
            char depth[16];
            if (marker_field(line, "depth:", depth, sizeof(depth), 0))
                msg.depth = atoi(depth);
            continue;
        }
        if (strcmp(line, "\fmessage}") == 0) {
            if (in_message) {
                emit_msg(r, &msg, is_first);
                is_first   = 0;
                msg_state_reset(&msg);
                in_message = 0;
            }
            continue;
        }
        if (strcmp(line, "\fheader{") == 0) { in_header = 1;  continue; }
        if (strcmp(line, "\fheader}") == 0) { in_header = 0;  continue; }
        if (strcmp(line, "\fbody{")   == 0) { continue; }
        if (strcmp(line, "\fbody}")   == 0) { continue; }

        if (starts_with(line, "\fpart{")) {
            int mode = 0;
            char ct[128] = "";
            marker_field(line, "Content-type:", ct, sizeof(ct), 1);
            if (strncasecmp(ct, "text/plain", 10) == 0) {
                mode             = 1;
                msg.have_plain   = 1;
            } else if (strncasecmp(ct, "text/html", 9) == 0 && !msg.have_plain) {
                mode = 2;
            }
            if (pstack_depth < PART_DEPTH_MAX)
                pstack_mode[pstack_depth++] = mode;
            continue;
        }
        if (strcmp(line, "\fpart}") == 0) {
            if (pstack_depth > 0) pstack_depth--;
            continue;
        }

        if (starts_with(line, "\fattachment{")) {
            memset(&cur_att, 0, sizeof(cur_att));
            char id[16] = "";
            if (marker_field(line, "ID:", id, sizeof(id), 1))
                cur_att.part_id = atoi(id);
            marker_field(line, "Content-type:", cur_att.content_type,
                         sizeof(cur_att.content_type), 1);
            marker_field(line, "Filename:", cur_att.filename,
                         sizeof(cur_att.filename), 1);
            snprintf(cur_att.msg_id, sizeof(cur_att.msg_id), "%s", msg.msg_id);
            in_attachment = 1;
            continue;
        }
        if (strcmp(line, "\fattachment}") == 0) {
            if (in_attachment) attach_push(r, &cur_att);
            in_attachment = 0;
            continue;
        }

        /* --- content ------------------------------------------------- */
        if (in_header) {
            const char *v;
            if      (header_match(line, "From",    &v))
                snprintf(msg.from, sizeof(msg.from), "%s", v);
            else if (header_match(line, "To",      &v))
                snprintf(msg.to, sizeof(msg.to), "%s", v);
            else if (header_match(line, "Cc",      &v))
                snprintf(msg.cc, sizeof(msg.cc), "%s", v);
            else if (header_match(line, "Subject", &v))
                snprintf(msg.subject, sizeof(msg.subject), "%s", v);
            else if (header_match(line, "Date",    &v))
                snprintf(msg.date, sizeof(msg.date), "%s", v);
            continue;
        }

        if (in_attachment) {
            /* notmuch emits a "Non-text part: …" placeholder we don't want. */
            continue;
        }

        /* Body content — only when the innermost open part says so. */
        if (pstack_depth > 0) {
            int mode = pstack_mode[pstack_depth - 1];
            size_t llen = strlen(line);
            if (mode == 1)
                buf_append(&msg.plain, &msg.plain_len, &msg.plain_cap,
                           line, llen, 1);
            else if (mode == 2)
                buf_append(&msg.html,  &msg.html_len,  &msg.html_cap,
                           line, llen, 1);
        }
    }

    if (in_message) {
        emit_msg(r, &msg, is_first);
        msg_state_reset(&msg);
    }
}
