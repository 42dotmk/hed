#include "mail_parse.h"
#include "lib/strbuf.h"
#include "lib/strutil.h"
#include "lib/vector.h"
#include "utils/term_cmd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

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
    int order;   /* position in hml show's output */
    time_t when; /* Date: as an epoch (see msg_save for unparseable) */

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

/* RFC 2822 "Tue, 18 May 2026 10:14:00 +0200" → epoch; -1 when it
 * doesn't parse. A zone that isn't numeric counts as UTC. */
static time_t parse_rfc2822(const char *s) {
    while (*s == ' ' || *s == '\t')
        s++;
    if (strlen(s) > 5 && s[3] == ',')
        s += 4;
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    const char *p = strptime(s, "%d %b %Y %H:%M", &tm);
    if (!p)
        return -1;
    if (*p == ':') {
        int sec = 0;
        p++;
        while (*p >= '0' && *p <= '9')
            sec = sec * 10 + (*p++ - '0');
        tm.tm_sec = sec;
    }
    while (*p == ' ' || *p == '\t')
        p++;
    long off = 0;
    if ((*p == '+' || *p == '-') && strlen(p) >= 5) {
        int sign = *p == '-' ? -1 : 1;
        int hh = (p[1] - '0') * 10 + (p[2] - '0');
        int mm = (p[3] - '0') * 10 + (p[4] - '0');
        off = sign * (hh * 3600L + mm * 60L);
    }
    time_t t = timegm(&tm);
    return t == (time_t)-1 ? -1 : t - off;
}

static void span_fill(MailMsgSpan *span, const MsgState *m) {
    snprintf(span->msg_id, sizeof(span->msg_id), "%s", m->msg_id);
    span->attach_start = m->attach_start;
    span->attach_count = m->attach_count;
    snprintf(span->from, sizeof(span->from), "%s", m->from);
    snprintf(span->to, sizeof(span->to), "%s", m->to);
    snprintf(span->cc, sizeof(span->cc), "%s", m->cc);
    snprintf(span->subject, sizeof(span->subject), "%s", m->subject);
    snprintf(span->date, sizeof(span->date), "%s", m->date);
}

/* "Attachments:  [n] name  [n] name" — the numbers are 1-based,
 * thread-wide (what :mail-attach <n> takes). Nothing when none. */
static void emit_attachments(MailRender *r, const MsgState *m) {
    int n_att = m->attach_count;
    if (n_att <= 0)
        return;
    size_t cap = 32 + (size_t)n_att * 80;
    char *al = malloc(cap);
    if (!al)
        return;
    size_t off = (size_t)snprintf(al, cap, "Attachments:");
    for (int i = 0; i < n_att; i++) {
        const MailAttachInfo *a = &r->attaches[m->attach_start + i];
        off += (size_t)snprintf(al + off, cap - off, "  [%d] %s",
                                m->attach_start + i + 1,
                                a->filename[0] ? a->filename : "(unnamed)");
    }
    lines_pushz(r, al);
    free(al);
}

/* Body: prefer plain (minus leading/trailing blank lines). Fall back
 * to html via w3m/lynx. */
static void emit_body(MailRender *r, const MsgState *m) {
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
    span_fill(&span, m);

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
    emit_attachments(r, m);
    lines_pushz(r, "");
    span.body_row = (int)arrlen(r->lines);
    arrput(r->msgs, span);

    emit_body(r, m);
}

/* ------------------------------------------------------------------ */
/* Chat view                                                           */
/* ------------------------------------------------------------------ */

static const char *rtrim_end(const char *s) {
    const char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r'))
        e--;
    return e;
}

static const char *ltrim(const char *s) {
    while (*s == ' ' || *s == '\t')
        s++;
    return s;
}

static int line_blank(const char *s) { return *ltrim(s) == '\0'; }

/* "On Tue, 18 May 2026, Alice wrote:" and its translations — the line
 * that introduces a quoted reply. */
static int is_attribution(const char *line) {
    const char *s = ltrim(line);
    const char *e = rtrim_end(s);
    if (e == s || e[-1] != ':')
        return 0;
    static const char *const verbs[] = {"wrote",   "writes", "schrieb", "écrit",
                                        "escribi", "napisa", "scritto", NULL};
    for (int i = 0; verbs[i]; i++)
        if (strcasestr(s, verbs[i]))
            return 1;
    return 0;
}

/* First line of a wrapped attribution ("On Tue, 18 May 2026 at 10:14
 * Alice Smith" / "Le mar. 18 mai …" / "Am 18.05.2026 um …"). */
static int is_attribution_head(const char *line) {
    const char *s = ltrim(line);
    static const char *const heads[] = {"On ", "Le ", "Am ",
                                        "El ", "Il ", NULL};
    for (int i = 0; heads[i]; i++)
        if (strncmp(s, heads[i], strlen(heads[i])) == 0)
            return 1;
    return 0;
}

/* A line after which nothing of the writer's own text follows:
 * signature separator, Outlook's original-message divider, forwarded-
 * message banners, mobile footers. */
static int is_cut_marker(const char *line) {
    const char *s = ltrim(line);
    const char *e = rtrim_end(s);
    size_t n = (size_t)(e - s);
    if (n == 2 && s[0] == '-' && s[1] == '-')
        return 1; /* "-- " (RFC 3676) */
    if (n >= 20) {
        size_t i = 0;
        while (i < n && s[i] == '_')
            i++;
        if (i == n)
            return 1; /* Outlook's ______ rule */
    }
    if (n > 10 && strncmp(s, "-----", 5) == 0 &&
        strncmp(e - 5, "-----", 5) == 0)
        return 1; /* -----Original Message----- (any language) */
    static const char *const heads[] = {
        "---------- Forwarded message", "Begin forwarded message",
        "Sent from my ", "Get Outlook for ", NULL};
    for (int i = 0; heads[i]; i++)
        if (strncasecmp(s, heads[i], strlen(heads[i])) == 0)
            return 1;
    return 0;
}

static int starts_with_ci(const char *s, const char *p) {
    return strncasecmp(s, p, strlen(p)) == 0;
}

/* Outlook-style reply header pasted without a divider: a "From:" line
 * followed within a few lines by "Subject:" and "To:"/"Sent:"/"Date:". */
static int is_header_block(char **lines, int i, int end) {
    if (!starts_with_ci(ltrim(lines[i]), "From:"))
        return 0;
    int subj = 0, other = 0;
    for (int k = i + 1; k < end && k <= i + 5; k++) {
        const char *s = ltrim(lines[k]);
        if (starts_with_ci(s, "Subject:"))
            subj = 1;
        else if (starts_with_ci(s, "To:") || starts_with_ci(s, "Sent:") ||
                 starts_with_ci(s, "Date:"))
            other = 1;
    }
    return subj && other;
}

/* w3m's trailing link list: "References:" then "[1] http…" lines. */
static int is_w3m_refs(char **lines, int i, int end) {
    if (strncmp(lines[i], "References:", 11) != 0 || !line_blank(lines[i] + 11))
        return 0;
    for (int k = i + 1; k < end; k++) {
        if (line_blank(lines[k]))
            continue;
        return ltrim(lines[k])[0] == '[';
    }
    return 0;
}

/* Trimmed line text with any *…* / _…_ emphasis (w3m, markdown-ish
 * mail) peeled off — what a signature line compares as. */
static void plain_text(const char *line, char *out, size_t cap) {
    const char *s = ltrim(line);
    const char *e = rtrim_end(s);
    while (s < e && (*s == '*' || *s == '_'))
        s++;
    while (e > s && (e[-1] == '*' || e[-1] == '_'))
        e--;
    size_t n = (size_t)(e - s);
    if (n >= cap)
        n = cap - 1;
    memcpy(out, s, n);
    out[n] = '\0';
}

/* A signature without the "-- " separator: a short trailing block
 * (≤ 15 lines of ≤ 80 chars) that opens with the sender's display name
 * on a line of its own — the mail-client-generated kind (name, title,
 * company, phone, links). Returns the row it starts at, or `end`. */
static int sig_start(char **ln, int from, int end, const char *name) {
    if (!name || strlen(name) < 3)
        return end;
    for (int i = from + 1; i < end; i++) {
        char t[256];
        plain_text(ln[i], t, sizeof(t));
        if (strcasecmp(t, name) != 0)
            continue;
        if (end - i > 15)
            continue;
        int ok = 1;
        for (int k = i; k < end && ok; k++)
            ok = strlen(ln[k]) <= 80;
        if (ok)
            return i;
    }
    return end;
}

/* Chat view: drop what the thread already shows or that says nothing —
 * quoted lines and their attribution, everything from a cut marker
 * down, a trailing signature block — then collapse blank runs. Works
 * on r->lines[from..]. */
static void chat_strip(MailRender *r, int from, const char *name) {
    int n = (int)arrlen(r->lines);
    if (from >= n)
        return;
    char **ln = r->lines;

    /* With ">" quotes the message may interleave answers between
     * quoted chunks, so attributions are dropped line-wise; without
     * them (HTML rendered by w3m) the attribution begins the quoted
     * tail and everything after it goes. */
    int has_quote = 0;
    for (int i = from; i < n; i++)
        if (ln[i][0] == '>')
            has_quote = 1;

    char *drop = calloc((size_t)n, 1);
    if (!drop)
        return;
    int end = n;
    for (int i = from; i < end; i++) {
        const char *l = ln[i];
        if (is_cut_marker(l) || is_header_block(ln, i, end) ||
            is_w3m_refs(ln, i, end)) {
            end = i;
            break;
        }
        if (l[0] == '>') {
            drop[i] = 1;
            continue;
        }
        if (is_attribution(l)) {
            int j = i;
            /* A wrapped attribution: back up to its "On …" head, at
             * most two lines above and only through non-blank text. */
            if (!is_attribution_head(l)) {
                for (int k = i - 1; k >= from && k >= i - 2; k--) {
                    if (drop[k] || line_blank(ln[k]))
                        break;
                    if (is_attribution_head(ln[k])) {
                        j = k;
                        break;
                    }
                }
            }
            if (!has_quote) {
                end = j;
                break;
            }
            for (int k = j; k <= i; k++)
                drop[k] = 1;
        }
    }

    /* Compact in place: skip dropped lines, collapse blank runs. */
    int out = from;
    for (int i = from; i < end; i++) {
        if (drop[i] ||
            (line_blank(ln[i]) && (out == from || line_blank(ln[out - 1])))) {
            free(ln[i]);
            continue;
        }
        ln[out++] = ln[i];
    }
    for (int i = end; i < n; i++)
        free(ln[i]);
    int sig = sig_start(ln, from, out, name);
    while (out > sig)
        free(ln[--out]);
    while (out > from && line_blank(ln[out - 1]))
        free(ln[--out]);
    arrsetlen(ln, out);
    r->lines = ln;
    free(drop);

    if (out == from)
        lines_pushz(r, "(quoted text only)");
}

/* The address part of "Name <addr>" (or a bare address). */
static const char *from_addr(const char *from, size_t *len) {
    const char *lt = strchr(from, '<');
    const char *addr = lt ? lt + 1 : from;
    *len = lt ? strcspn(addr, ">") : strlen(addr);
    return addr;
}

static int is_self(const char *from, const char *self) {
    if (!self || !*self)
        return 0;
    size_t alen, slen;
    const char *addr = from_addr(from, &alen);
    const char *saddr = from_addr(self, &slen);
    return slen && slen == alen && strncasecmp(saddr, addr, alen) == 0;
}

/* Display name out of "Alice Smith <alice@example.com>", the bare
 * address when there is none. */
static void from_name(const char *from, char *out, size_t cap) {
    const char *lt = strchr(from, '<');
    size_t alen;
    const char *addr = from_addr(from, &alen);

    const char *s = from;
    const char *e = lt ? lt : from + strlen(from);
    while (s < e && (*s == ' ' || *s == '"'))
        s++;
    while (e > s && (e[-1] == ' ' || e[-1] == '"'))
        e--;
    if (e == s) {
        s = addr;
        e = addr + alen;
    }
    size_t n = (size_t)(e - s);
    if (n >= cap)
        n = cap - 1;
    memcpy(out, s, n);
    out[n] = '\0';
}

/* "Tue, 18 May 2026 10:14:00 +0200" → "18 May 2026 10:14", in local
 * time when the date parses (senders write theirs in their own zone;
 * a conversation reads in one). */
static void chat_date(const char *date, char *out, size_t cap) {
    time_t when = parse_rfc2822(date);
    if (when != -1) {
        struct tm lt;
        if (localtime_r(&when, &lt) &&
            strftime(out, cap, "%d %b %Y %H:%M", &lt))
            return;
    }
    const char *s = ltrim(date);
    if (strlen(s) > 5 && s[3] == ',')
        s = ltrim(s + 4);
    size_t n = 0;
    while (*s && n + 1 < cap) {
        /* Cut inside the time token after HH:MM. */
        if (*s == ':' && n >= 2 && s[1] >= '0' && s[1] <= '9' && s[2] >= '0' &&
            s[2] <= '9' && (s[3] == ':' || s[3] == ' ' || s[3] == '\0')) {
            out[n++] = ':';
            out[n++] = s[1];
            out[n++] = s[2];
            break;
        }
        out[n++] = *s++;
    }
    while (n > 0 && out[n - 1] == ' ')
        n--;
    out[n] = '\0';
}

static void emit_chat_msg(MailRender *r, MsgState *m, const char *self) {
    MailMsgSpan span;
    memset(&span, 0, sizeof(span));
    span.row = (int)arrlen(r->lines);
    if (arrlen(r->msgs) > 0)
        lines_pushz(r, "");
    span.hdr_row = (int)arrlen(r->lines);
    span_fill(&span, m);

    char name[256], when[128], line[512];
    from_name(m->from[0] ? m->from : "(unknown)", name, sizeof(name));
    const char *who = is_self(m->from, self) ? "You" : name;
    chat_date(m->date, when, sizeof(when));
    if (when[0])
        snprintf(line, sizeof(line), "● %s — %s", who, when);
    else
        snprintf(line, sizeof(line), "● %s", who);
    lines_pushz(r, line);
    emit_attachments(r, m);

    span.body_row = (int)arrlen(r->lines);
    arrput(r->msgs, span);

    emit_body(r, m);
    chat_strip(r, span.body_row, name);
}

static int by_date(const void *a, const void *b) {
    const MsgState *x = a, *y = b;
    if (x->when != y->when)
        return x->when < y->when ? -1 : 1;
    return x->order - y->order;
}

/* Push the current MsgState onto saved[] (taking ownership of its
 * heap buffers) and reset the working copy. The actual emit happens
 * at the end of parsing in reverse order so the newest message lands
 * at the top of the rendered buffer. */
static void msg_save(MsgState *m, MsgState **saved, int attach_total) {
    m->attach_count = attach_total - m->attach_start;
    m->order = (int)arrlen(*saved);
    m->when = parse_rfc2822(m->date);
    /* An unparseable date keeps its thread-order slot: inherit the
     * previous message's time so the sort stays consistent. */
    if (m->when == -1)
        m->when = m->order ? (*saved)[m->order - 1].when : 0;
    arrput(*saved, *m);
    /* Ownership of plain/html moved into the saved entry — wipe the
     * working copy so it isn't double-freed. */
    memset(m, 0, sizeof(*m));
}

void mail_render_show_text(MailRender *r, char **raw, int raw_count, int chat,
                           const char *self) {
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

    /* Full view: newest-first. hml show outputs the thread in
     * arrival/depth order (root → replies), which is oldest-first;
     * reversing puts the most recent message at the top of the
     * buffer — what the reader actually wants to see. The chat view
     * keeps the conversation order and reads top to bottom. */
    ptrdiff_t cnt = arrlen(saved);
    if (chat && cnt > 0) {
        /* hml show walks the reply tree, not the clock: a conversation
         * reads by date (thread order breaks ties, and stands in for
         * unparseable dates). */
        qsort(saved, (size_t)cnt, sizeof(*saved), by_date);
        char line[600];
        snprintf(line, sizeof(line), "Subject: %s", saved[0].subject);
        lines_pushz(r, line);
        lines_pushz(r, "");
    }
    for (ptrdiff_t k = 0; k < cnt; k++) {
        ptrdiff_t i = chat ? k : cnt - 1 - k;
        if (chat)
            emit_chat_msg(r, &saved[i], self);
        else
            emit_msg(r, &saved[i], k == 0);
    }
    /* The newest message's HTML is the one :mail-open-html shows. */
    for (ptrdiff_t i = cnt - 1; i >= 0; i--) {
        if (!r->html && saved[i].html.len > 0) {
            r->html = saved[i].html.data;
            r->html_len = saved[i].html.len;
            saved[i].html = strbuf_new();
        }
        msg_state_reset(&saved[i]);
    }
    arrfree(saved);
}
