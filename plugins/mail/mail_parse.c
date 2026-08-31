#include "mail_parse.h"
#include "lib/strbuf.h"
#include "lib/strutil.h"
#include "lib/vector.h"
#include "utils/term_cmd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define PART_DEPTH_MAX 8

static void lines_push(MailRender *r, const char *s, size_t len) {
    char *dup = malloc(len + 1);
    if (!dup)
        return;
    if (len)
        memcpy(dup, s, len);
    dup[len] = '\0';
    arrput(r->lines, dup);
}

static void lines_pushz(MailRender *r, const char *s) {
    lines_push(r, s, strlen(s));
}

void mail_render_init(MailRender *r) { memset(r, 0, sizeof(*r)); }

void mail_render_free(MailRender *r) {
    for (ptrdiff_t i = 0; i < arrlen(r->lines); i++)
        free(r->lines[i]);
    arrfree(r->lines);
    arrfree(r->attaches);
    arrfree(r->msgs);
    free(r->html);
    memset(r, 0, sizeof(*r));
}

/* Parse `key: value` out of one of the inline marker lines (notmuch's
 * text format, as printed by `hml show --format=text`).
 * `comma_sep` controls termination:
 *   1 — comma-separated (\fpart{, \fattachment{): value runs until ", "
 *   0 — space-separated (\fmessage{): value runs until next space */
static int marker_field(const char *line, const char *key, char *out,
                        size_t cap, int comma_sep) {
    const char *p = strstr(line, key);
    if (!p)
        return 0;
    p += strlen(key);
    while (*p == ' ' || *p == '\t')
        p++;
    size_t n = 0;
    while (*p && n + 1 < cap) {
        if (comma_sep) {
            if (p[0] == ',' && p[1] == ' ')
                break;
        } else {
            if (*p == ' ' || *p == '\t')
                break;
        }
        out[n++] = *p++;
    }
    while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\t'))
        n--;
    out[n] = '\0';
    return 1;
}

static int header_match(const char *line, const char *name,
                        const char **value) {
    size_t nlen = strlen(name);
    if (strncasecmp(line, name, nlen) != 0)
        return 0;
    if (line[nlen] != ':')
        return 0;
    const char *p = line + nlen + 1;
    while (*p == ' ' || *p == '\t')
        p++;
    *value = p;
    return 1;
}

/* Run `w3m -dump -T text/html` over `html`, splitting stdout into lines
 * appended to `out`. Falls back to `lynx -dump -stdin` if w3m is missing.
 * On total failure, emits a single placeholder line. */
static void render_html(const char *html, size_t len, MailRender *out) {
    const char *cmds[] = {
        "w3m -dump -T text/html -o display_link_number=true 2>/dev/null",
        "lynx -dump -stdin 2>/dev/null", NULL};
    for (int i = 0; cmds[i]; i++) {
        char *buf = NULL;
        size_t blen = 0;
        int rc = term_cmd_filter(cmds[i], html, len, &buf, &blen);
        if (rc == 0 && buf && blen > 0) {
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
    int depth;

    StrBuf plain; /* text/plain body accumulator */
    StrBuf html;  /* fallback text/html accumulator */
    int have_plain;

    /* indices into render->attaches for this message */
    int attach_start;
    /* number of attachments belonging to this message — captured when
     * the message is closed so emit_msg works even when messages are
     * emitted out of parse order (newest-first reordering). */
    int attach_count;
} MsgState;

static void msg_state_reset(MsgState *m) {
    strbuf_free(&m->plain);
    strbuf_free(&m->html);
    memset(m, 0, sizeof(*m));
}

static int span_blank(const char *s, size_t len) {
    for (size_t i = 0; i < len; i++)
        if (s[i] != ' ' && s[i] != '\t')
            return 0;
    return 1;
}

static void emit_msg(MailRender *r, MsgState *m, int is_first) {
    MailMsgSpan span;
    memset(&span, 0, sizeof(span));
    span.row = (int)arrlen(r->lines);
    if (!is_first) {
        lines_pushz(r, "");
        lines_pushz(r, "──────────────────────────────────────────");
        lines_pushz(r, "");
    }
    span.hdr_row = (int)arrlen(r->lines);
    snprintf(span.msg_id, sizeof(span.msg_id), "%s", m->msg_id);
    span.attach_start = m->attach_start;
    span.attach_count = m->attach_count;
    arrput(r->msgs, span);

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

    int n_att = m->attach_count;
    if (n_att > 0) {
        size_t cap = 32 + (size_t)n_att * 80;
        char *al = malloc(cap);
        if (al) {
            size_t off = (size_t)snprintf(al, cap, "Attachments:");
            for (int i = 0; i < n_att; i++) {
                const MailAttachInfo *a = &r->attaches[m->attach_start + i];
                /* 1-based whole-thread attachment number — what
                 * :mail-attach <n> takes. */
                off += (size_t)snprintf(
                    al + off, cap - off, "  [%d] %s", m->attach_start + i + 1,
                    a->filename[0] ? a->filename : "(unnamed)");
            }
            lines_pushz(r, al);
            free(al);
        }
    }

    lines_pushz(r, "");

    /* Body: prefer plain. Fall back to html via w3m/lynx. */
    if (m->have_plain && m->plain.len > 0) {
        const char *body = m->plain.data;
        size_t body_len = m->plain.len;

        /* Collect line spans, then drop leading/trailing blank lines. */
        typedef struct {
            size_t off, len;
        } LineSpan;
        LineSpan *ls = NULL;
        for (size_t a = 0;;) {
            size_t b = a;
            while (b < body_len && body[b] != '\n')
                b++;
            LineSpan sp = {a, b - a};
            arrput(ls, sp);
            if (b >= body_len)
                break;
            a = b + 1;
        }
        ptrdiff_t lo = 0, hi = arrlen(ls) - 1;
        while (lo <= hi && span_blank(body + ls[lo].off, ls[lo].len))
            lo++;
        while (hi >= lo && span_blank(body + ls[hi].off, ls[hi].len))
            hi--;
        for (ptrdiff_t i = lo; i <= hi; i++)
            lines_push(r, body + ls[i].off, ls[i].len);
        arrfree(ls);
    } else if (m->html.data && m->html.len > 0) {
        render_html(m->html.data, m->html.len, r);
    } else {
        lines_pushz(r, "(empty body)");
    }
}

/* Push the current MsgState onto saved[] (taking ownership of its
 * heap buffers) and reset the working copy. The actual emit happens
 * at the end of parsing in reverse order so the newest message lands
 * at the top of the rendered buffer. */
static void msg_save(MsgState *m, MsgState **saved, int attach_total) {
    m->attach_count = attach_total - m->attach_start;
    arrput(*saved, *m);
    /* Ownership of plain/html moved into the saved entry — wipe the
     * working copy so it isn't double-freed. */
    memset(m, 0, sizeof(*m));
}

void mail_render_show_text(MailRender *r, char **raw, int raw_count) {
    MsgState msg;
    memset(&msg, 0, sizeof(msg));
    int in_message = 0;

    MsgState *saved = NULL;

    /* Header block flag (between \fheader{ and \fheader}). */
    int in_header = 0;

    /* Part stack: each level tracks whether we're capturing into plain/html. */
    int pstack_depth = 0;
    int pstack_mode[PART_DEPTH_MAX]; /* 0=skip, 1=plain, 2=html */
    /* multipart suppression: when 1, the wrapper part itself isn't capturing
     * but its children may. We only track via the per-level mode. */

    /* Attachment context: filename/type discovered at \fattachment{.
     * notmuch 0.40 closes the block with \fpart} (hml with \fattachment}), so
     * the attachment opens a part-stack level like any other part and is
     * registered when its level closes; \fattachment} is accepted too
     * for versions that emit it. */
    MailAttachInfo cur_att;
    int in_attachment = 0;
    int attach_depth = 0;

    for (int i = 0; i < raw_count; i++) {
        const char *line = raw[i] ? raw[i] : "";

        /* --- markers ------------------------------------------------- */
        if (str_starts_with(line, "\fmessage{")) {
            if (in_message)
                msg_save(&msg, &saved, (int)arrlen(r->attaches));
            in_message = 1;
            in_header = 0;
            in_attachment = 0;
            pstack_depth = 0;
            msg.attach_start = (int)arrlen(r->attaches);
            marker_field(line, "id:", msg.msg_id, sizeof(msg.msg_id), 0);
            char depth[16];
            if (marker_field(line, "depth:", depth, sizeof(depth), 0))
                msg.depth = atoi(depth);
            continue;
        }
        if (strcmp(line, "\fmessage}") == 0) {
            if (in_message) {
                msg_save(&msg, &saved, (int)arrlen(r->attaches));
                in_message = 0;
            }
            continue;
        }
        if (strcmp(line, "\fheader{") == 0) {
            in_header = 1;
            continue;
        }
        if (strcmp(line, "\fheader}") == 0) {
            in_header = 0;
            continue;
        }
        if (strcmp(line, "\fbody{") == 0) {
            continue;
        }
        if (strcmp(line, "\fbody}") == 0) {
            continue;
        }

        if (str_starts_with(line, "\fpart{")) {
            int mode = 0;
            char ct[128] = "";
            marker_field(line, "Content-type:", ct, sizeof(ct), 1);
            if (strncasecmp(ct, "text/plain", 10) == 0) {
                mode = 1;
                msg.have_plain = 1;
            } else if (strncasecmp(ct, "text/html", 9) == 0) {
                /* Captured even when a plain part exists: the display
                 * prefers plain, but the raw HTML is kept on the render
                 * for opening in an external browser. */
                mode = 2;
            }
            if (pstack_depth < PART_DEPTH_MAX)
                pstack_mode[pstack_depth++] = mode;
            continue;
        }
        if (strcmp(line, "\fpart}") == 0) {
            if (in_attachment && pstack_depth == attach_depth) {
                arrput(r->attaches, cur_att);
                in_attachment = 0;
            }
            if (pstack_depth > 0)
                pstack_depth--;
            continue;
        }

        if (str_starts_with(line, "\fattachment{")) {
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
            if (pstack_depth < PART_DEPTH_MAX)
                pstack_mode[pstack_depth++] = 0;
            attach_depth = pstack_depth;
            continue;
        }
        if (strcmp(line, "\fattachment}") == 0) {
            if (in_attachment) {
                arrput(r->attaches, cur_att);
                in_attachment = 0;
                if (pstack_depth > 0)
                    pstack_depth--;
            }
            continue;
        }

        /* --- content ------------------------------------------------- */
        if (in_header) {
            const char *v;
            if (header_match(line, "From", &v))
                snprintf(msg.from, sizeof(msg.from), "%s", v);
            else if (header_match(line, "To", &v))
                snprintf(msg.to, sizeof(msg.to), "%s", v);
            else if (header_match(line, "Cc", &v))
                snprintf(msg.cc, sizeof(msg.cc), "%s", v);
            else if (header_match(line, "Subject", &v))
                snprintf(msg.subject, sizeof(msg.subject), "%s", v);
            else if (header_match(line, "Date", &v))
                snprintf(msg.date, sizeof(msg.date), "%s", v);
            continue;
        }

        if (in_attachment) {
            /* the "Non-text part: …" placeholder line — not wanted. */
            continue;
        }

        /* Body content — only when the innermost open part says so.
         * Each captured line carries a trailing newline. */
        if (pstack_depth > 0) {
            int mode = pstack_mode[pstack_depth - 1];
            size_t llen = strlen(line);
            StrBuf *acc = (mode == 1)   ? &msg.plain
                          : (mode == 2) ? &msg.html
                                        : NULL;
            if (acc) {
                strbuf_append(acc, line, llen);
                strbuf_append_char(acc, '\n');
            }
        }
    }

    if (in_message)
        msg_save(&msg, &saved, (int)arrlen(r->attaches));

    /* Emit messages newest-first. hml show outputs the thread in
     * arrival/depth order (root → replies), which is oldest-first;
     * reversing puts the most recent message at the top of the
     * buffer — what the reader actually wants to see. */
    for (ptrdiff_t i = arrlen(saved) - 1; i >= 0; i--) {
        emit_msg(r, &saved[i], i == arrlen(saved) - 1);
        if (!r->html && saved[i].html.len > 0) {
            r->html = saved[i].html.data;
            r->html_len = saved[i].html.len;
            saved[i].html = strbuf_new();
        }
        msg_state_reset(&saved[i]);
    }
    arrfree(saved);
}
