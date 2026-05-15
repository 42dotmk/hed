#include "mail.h"
#include "hed.h"
#include "buf/row.h"
#include "lib/theme.h"
#include "prompt.h"
#include "utils/term_cmd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAIL_MAX      500
#define MAIL_LIST_BUF "mail://list"

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    char thread_id[128]; /* "thread:0000000000001234" */
    char display[512];   /* rest of the notmuch summary line */
    int  is_unread;      /* 1 if the "unread" tag is present */
} MailEntry;

/* Check for "unread" as a whole word inside the last (...) tag group. */
static int has_unread_tag(const char *line) {
    const char *last_paren = strrchr(line, '(');
    if (!last_paren) return 0;
    const char *p = last_paren + 1;
    while (*p && *p != ')') {
        while (*p == ' ') p++;
        const char *word = p;
        while (*p && *p != ' ' && *p != ')') p++;
        size_t wlen = (size_t)(p - word);
        if (wlen == 6 && memcmp(word, "unread", 6) == 0)
            return 1;
    }
    return 0;
}

static MailEntry mail_entries[MAIL_MAX];
static int       mail_entry_count = 0;

static char base_query[512]    = "tag:inbox";
static char filter_query[512]  = "";
static char mbsync_profile[128] = "-a";

/* Forward-declared internal helper from buf/row.c */
void buf_row_insert_in(Buffer *buf, int at, const char *s, size_t len);

/* ------------------------------------------------------------------ */
/* Query helpers                                                       */
/* ------------------------------------------------------------------ */

void mail_set_query(const char *q) {
    snprintf(base_query, sizeof(base_query), "%s", q ? q : "tag:inbox");
}

const char *mail_get_query(void) { return base_query; }

void mail_set_filter(const char *f) {
    snprintf(filter_query, sizeof(filter_query), "%s", f ? f : "");
}

static void build_full_query(char *out, size_t sz) {
    if (filter_query[0])
        snprintf(out, sz, "%s AND %s", base_query, filter_query);
    else
        snprintf(out, sz, "%s", base_query);
}

/* ------------------------------------------------------------------ */
/* mbsync                                                              */
/* ------------------------------------------------------------------ */

void mail_set_mbsync_profile(const char *profile) {
    snprintf(mbsync_profile, sizeof(mbsync_profile), "%s",
             profile ? profile : "-a");
}

void mail_sync(void) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "mbsync %s", mbsync_profile);
    ed_set_status_message("mail: running mbsync %s ...", mbsync_profile);
    int rc = term_cmd_system(cmd);
    if (rc != 0) {
        ed_set_status_message("mail: mbsync exited with status %d", rc);
        return;
    }
    term_cmd_system("notmuch new 2>/dev/null");
    ed_set_status_message("mail: sync complete");
    mail_open_list();
}

/* ------------------------------------------------------------------ */
/* Buffer helper                                                       */
/* ------------------------------------------------------------------ */

static void clear_buffer(Buffer *buf) {
    for (int i = 0; i < buf->num_rows; i++)
        row_free(&buf->rows[i]);
    free(buf->rows);
    buf->rows     = NULL;
    buf->num_rows = 0;
}

/* ------------------------------------------------------------------ */
/* Highlighting                                                        */
/* ------------------------------------------------------------------ */

/* Colors for the mail list — unread entries are bold throughout. */
#define MC_UNREAD_FLAG    "\x1b[1;38;2;247;118;142m"  /* bold red            */
#define MC_UNREAD_COUNT   "\x1b[1;38;2;86;95;137m"    /* bold muted          */
#define MC_UNREAD_SENDER  "\x1b[1;38;2;122;162;247m"  /* bold blue           */
#define MC_UNREAD_SUBJECT "\x1b[1;38;2;192;202;245m"  /* bold fg             */
#define MC_READ_FLAG      COLOR_COMMENT                /* dim flag column     */
#define MC_READ_COUNT     COLOR_COMMENT                /* dim [N/M]           */
#define MC_READ_SENDER    COLOR_FUNCTION               /* blue                */
#define MC_READ_SUBJECT   COLOR_VARIABLE               /* normal fg           */
#define MC_META           COLOR_COMMENT                /* date / tags / dim   */

/* Colors for the mail message view. */
#define MC_MSG_MARKER     COLOR_DELIMITER              /* \fpart{ lines       */
#define MC_MSG_HDR_KEY    COLOR_KEYWORD                /* From: / Subject: …  */
#define MC_MSG_HDR_VAL    COLOR_VARIABLE               /* header value        */
#define MC_MSG_QUOTE      COLOR_COMMENT                /* > quoted lines      */

/* A coloured span: [s, e) bytes in the render buffer → SGR escape. */
typedef struct { int s, e; const char *sgr; } MailSpan;

/* Write the visible slice [col_off, col_off+max_cols) of `raw` into `dst`,
 * applying colour from `spans`.  Returns bytes written (including SGR codes),
 * or 0 on empty input. */
static size_t emit_spans(const char *raw, int raw_len,
                         const MailSpan *spans, int nspan,
                         char *dst, size_t dst_cap,
                         int col_off, int max_cols) {
    int end = col_off + max_cols;
    if (end > raw_len) end = raw_len;
    if (col_off >= end || dst_cap < 8) return 0;

    char       *p   = dst;
    const char *lim = dst + dst_cap - 32; /* headroom for one SGR + reset */
    const char *active_sgr = NULL;

    for (int b = col_off; b < end && p < lim; b++) {
        /* Find the span that owns byte b (linear scan — lines are short). */
        const char *sgr = NULL;
        for (int i = 0; i < nspan; i++) {
            if (spans[i].s <= b && b < spans[i].e) { sgr = spans[i].sgr; break; }
        }
        /* Emit SGR only on colour transitions. */
        if (sgr != active_sgr) {
            const char *code = sgr ? sgr : COLOR_RESET;
            size_t clen = strlen(code);
            if (p + (int)clen >= lim) break;
            memcpy(p, code, clen);
            p += clen;
            active_sgr = sgr;
        }
        *p++ = raw[b];
    }
    /* Always close with a reset so we don't bleed into adjacent cells. */
    if (p + 4 <= dst + (int)dst_cap) { memcpy(p, COLOR_RESET, 4); p += 4; }
    return (size_t)(p - dst);
}

/* Build colour spans for one mail-list row.
 * Row format (after our 2-char flag prefix):
 *   [N/M] sender1, sender2; Subject line (relative-date) (tags) */
static int parse_list_spans(const char *raw, int len,
                             MailSpan *sp, int max) {
    int n = 0;
    if (len < 2 || n + 1 > max) return 0;

    int unread = (raw[0] == 'U');

    /* 2-char flag column */
    sp[n++] = (MailSpan){ 0, 2, unread ? MC_UNREAD_FLAG : MC_READ_FLAG };
    int pos = 2;

    /* Thread count [N/M] followed by a space */
    if (pos < len && raw[pos] == '[' && n < max) {
        const char *close = memchr(raw + pos, ']', (size_t)(len - pos));
        if (close) {
            int end = (int)(close - raw) + 2; /* include '] ' */
            if (end > len) end = len;
            sp[n++] = (MailSpan){ pos, end, unread ? MC_UNREAD_COUNT : MC_READ_COUNT };
            pos = end;
        }
    }

    /* Sender: up to and including ';' */
    const char *semi = memchr(raw + pos, ';', (size_t)(len - pos));
    if (semi && n < max) {
        int end = (int)(semi - raw) + 1;
        sp[n++] = (MailSpan){ pos, end, unread ? MC_UNREAD_SENDER : MC_READ_SENDER };
        pos = end;
        if (pos < len && raw[pos] == ' ') pos++;
    }

    /* Subject: everything up to the last '(' (date/tag group) */
    int last_paren = -1;
    for (int i = len - 1; i >= pos; i--) {
        if (raw[i] == '(') { last_paren = i; break; }
    }
    if (last_paren > pos && n < max) {
        sp[n++] = (MailSpan){ pos, last_paren,
                              unread ? MC_UNREAD_SUBJECT : MC_READ_SUBJECT };
        pos = last_paren;
    }

    /* Date / tags: remainder */
    if (pos < len && n < max)
        sp[n++] = (MailSpan){ pos, len, MC_META };

    return n;
}

static size_t mail_list_hl(Buffer *buf, int row,
                           char *dst, size_t dst_cap,
                           int col_off, int max_cols) {
    if (row < 0 || row >= buf->num_rows) return 0;
    const char *raw = buf->rows[row].render.data;
    int         len = (int)buf->rows[row].render.len;
    if (!raw || len <= 0) return 0;

    MailSpan spans[16];
    int n = parse_list_spans(raw, len, spans, 16);
    return emit_spans(raw, len, spans, n, dst, dst_cap, col_off, max_cols);
}

/* Known RFC 2822 header names we want to colour. */
static const char *const MAIL_HEADERS[] = {
    "From:", "To:", "Cc:", "Bcc:", "Subject:", "Date:",
    "Reply-To:", "Message-Id:", "In-Reply-To:", "References:",
    NULL
};

/* Build colour spans for one mail-message row. */
static int parse_msg_spans(const char *raw, int len,
                            MailSpan *sp, int max) {
    int n = 0;
    if (len <= 0) return 0;

    /* notmuch format markers (\fpart{, \fheader}, …) */
    if (raw[0] == '\f') {
        if (n < max) sp[n++] = (MailSpan){ 0, len, MC_MSG_MARKER };
        return n;
    }

    /* Quoted lines */
    if (raw[0] == '>') {
        if (n < max) sp[n++] = (MailSpan){ 0, len, MC_MSG_QUOTE };
        return n;
    }

    /* RFC 2822 header lines */
    for (int i = 0; MAIL_HEADERS[i]; i++) {
        size_t hlen = strlen(MAIL_HEADERS[i]);
        if ((size_t)len > hlen &&
            strncasecmp(raw, MAIL_HEADERS[i], hlen) == 0) {
            if (n + 1 < max) {
                sp[n++] = (MailSpan){ 0,        (int)hlen, MC_MSG_HDR_KEY };
                sp[n++] = (MailSpan){ (int)hlen, len,      MC_MSG_HDR_VAL };
            }
            return n;
        }
    }

    return 0; /* body text: no highlight (fall back to plain) */
}

static size_t mail_msg_hl(Buffer *buf, int row,
                          char *dst, size_t dst_cap,
                          int col_off, int max_cols) {
    if (row < 0 || row >= buf->num_rows) return 0;
    const char *raw = buf->rows[row].render.data;
    int         len = (int)buf->rows[row].render.len;
    if (!raw || len <= 0) return 0;

    MailSpan spans[8];
    int n = parse_msg_spans(raw, len, spans, 8);
    if (n == 0) return 0; /* plain body: let renderer output raw bytes */
    return emit_spans(raw, len, spans, n, dst, dst_cap, col_off, max_cols);
}

/* ------------------------------------------------------------------ */
/* notmuch query → entries                                             */
/* ------------------------------------------------------------------ */

static void mail_run_query(void) {
    char query[1100];
    build_full_query(query, sizeof(query));

    char cmd[1300];
    snprintf(cmd, sizeof(cmd),
             "notmuch search --output=summary -- %s 2>/dev/null", query);

    char **lines = NULL;
    int    count = 0;
    term_cmd_capture(cmd, &lines, &count);

    mail_entry_count = 0;
    for (int i = 0; i < count && mail_entry_count < MAIL_MAX; i++) {
        const char *line = lines[i];
        if (!line || !line[0]) continue;

        MailEntry *e = &mail_entries[mail_entry_count];

        /* First token is the thread ID — ends at the first space. */
        const char *sp = strchr(line, ' ');
        if (sp) {
            size_t tlen = (size_t)(sp - line);
            if (tlen >= sizeof(e->thread_id))
                tlen = sizeof(e->thread_id) - 1;
            memcpy(e->thread_id, line, tlen);
            e->thread_id[tlen] = '\0';
            snprintf(e->display, sizeof(e->display), "%s", sp + 1);
        } else {
            snprintf(e->thread_id, sizeof(e->thread_id), "%s", line);
            e->display[0] = '\0';
        }
        e->is_unread = has_unread_tag(e->display);
        mail_entry_count++;
    }

    term_cmd_free(lines, count);
}

/* ------------------------------------------------------------------ */
/* Open list buffer                                                    */
/* ------------------------------------------------------------------ */

void mail_open_list(void) {
    mail_run_query();

    int idx      = -1;
    int existing = buf_find_by_filename(MAIL_LIST_BUF);
    if (existing >= 0) {
        buf_switch(existing);
        idx = existing;
    } else {
        if (buf_new(MAIL_LIST_BUF, &idx) != ED_OK) {
            ed_set_status_message("mail: failed to open buffer");
            return;
        }
    }

    Buffer *buf = &E.buffers[idx];
    free(buf->title);    buf->title    = strdup("Mail");
    free(buf->filetype); buf->filetype = strdup("mail");
    buf->readonly   = 1;
    buf->hl_line_fn = mail_list_hl;

    clear_buffer(buf);

    if (mail_entry_count == 0) {
        buf_row_insert_in(buf, 0, "(no messages)", 13);
    } else {
        char line[520];
        for (int i = 0; i < mail_entry_count; i++) {
            const char *flag = mail_entries[i].is_unread ? "U " : "  ";
            snprintf(line, sizeof(line), "%s%s", flag, mail_entries[i].display);
            buf_row_insert_in(buf, buf->num_rows, line, strlen(line));
        }
    }
    buf->dirty = 0;

    Window *win = window_cur();
    if (win) {
        win_attach_buf(win, buf);
        win->cursor.x = 0;
        win->cursor.y = 0;
    }
    E.current_buffer = idx;

    int unread = 0;
    for (int i = 0; i < mail_entry_count; i++)
        if (mail_entries[i].is_unread) unread++;
    int read = mail_entry_count - unread;

    char q[1100];
    build_full_query(q, sizeof(q));
    if (filter_query[0])
        ed_set_status_message(
            "mail: %d threads (%d unread, %d read)  [%s] (filtered: %s)",
            mail_entry_count, unread, read, base_query, filter_query);
    else
        ed_set_status_message(
            "mail: %d threads (%d unread, %d read)  [%s]",
            mail_entry_count, unread, read, base_query);
}

/* ------------------------------------------------------------------ */
/* Open thread on <CR>                                                 */
/* ------------------------------------------------------------------ */

void mail_handle_enter(void) {
    Buffer *buf = buf_cur();
    if (!buf || !buf->filetype || strcmp(buf->filetype, "mail") != 0) return;

    Window *win = window_cur();
    if (!win) return;

    int row = win->cursor.y;
    if (row < 0 || row >= mail_entry_count) return;

    const char *tid = mail_entries[row].thread_id;
    if (!tid[0]) return;

    /* Record current position so <C-o> returns to the mail list. */
    if (buf->filename)
        jump_list_add(&E.jump_list, buf->filename, win->cursor.x, win->cursor.y);

    /* Reuse an already-open thread buffer if present. */
    char bufname[256];
    snprintf(bufname, sizeof(bufname), "mail://%s", tid);

    int existing = buf_find_by_filename(bufname);
    if (existing >= 0) {
        buf_switch(existing);
        return;
    }

    int idx = -1;
    if (buf_new(bufname, &idx) != ED_OK) {
        ed_set_status_message("mail: failed to open thread buffer");
        return;
    }

    Buffer *tbuf = &E.buffers[idx];
    free(tbuf->title);    tbuf->title    = strdup(mail_entries[row].display);
    free(tbuf->filetype); tbuf->filetype = strdup("mail-message");
    tbuf->readonly   = 1;
    tbuf->hl_line_fn = mail_msg_hl;

    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "notmuch show --format=text -- %s 2>/dev/null", tid);

    char **lines = NULL;
    int    count = 0;
    term_cmd_capture(cmd, &lines, &count);

    clear_buffer(tbuf);
    for (int i = 0; i < count; i++) {
        const char *line = lines[i] ? lines[i] : "";
        buf_row_insert_in(tbuf, tbuf->num_rows, line, strlen(line));
    }
    term_cmd_free(lines, count);
    tbuf->dirty = 0;

    Window *cur_win = window_cur();
    if (cur_win) {
        win_attach_buf(cur_win, tbuf);
        cur_win->cursor.x = 0;
        cur_win->cursor.y = 0;
    }
    E.current_buffer = idx;

    ed_set_status_message("%s", mail_entries[row].display);
}

/* ------------------------------------------------------------------ */
/* Tagging                                                             */
/* ------------------------------------------------------------------ */

static int tag_char_ok(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' ||
           c == '/' || c == ':';
}

/* Normalize a whitespace-separated tag list into "+a -b +c" form.
 * Returns 0 on success, -1 on validation error (status message is set). */
static int parse_tag_args(const char *args, char *out, size_t cap) {
    if (!args || !*args) {
        ed_set_status_message("mail-tag: usage :mail-tag <+tag|-tag>...");
        return -1;
    }
    size_t n = 0;
    const char *p = args;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        const char *tok = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        size_t tlen = (size_t)(p - tok);
        if (tlen == 0) continue;

        char sign = '+';
        const char *body = tok;
        size_t blen = tlen;
        if (*tok == '+' || *tok == '-') {
            sign = *tok;
            body++;
            blen--;
        }
        if (blen == 0) {
            ed_set_status_message("mail-tag: empty tag");
            return -1;
        }
        for (size_t i = 0; i < blen; i++) {
            if (!tag_char_ok(body[i])) {
                ed_set_status_message("mail-tag: invalid char in tag");
                return -1;
            }
        }
        if (n + blen + 3 >= cap) {
            ed_set_status_message("mail-tag: too many tags");
            return -1;
        }
        if (n) out[n++] = ' ';
        out[n++] = sign;
        memcpy(out + n, body, blen);
        n += blen;
    }
    out[n] = '\0';
    if (n == 0) {
        ed_set_status_message("mail-tag: no tags given");
        return -1;
    }
    return 0;
}

/* Re-render the list, keeping cursor row when possible. */
static void mail_refresh_keep_cursor(void) {
    Window *win = window_cur();
    int saved_y = win ? win->cursor.y : 0;
    int saved_x = win ? win->cursor.x : 0;
    mail_open_list();
    win = window_cur();
    if (win) {
        Buffer *lbuf = buf_cur();
        if (lbuf && saved_y < lbuf->num_rows) {
            win->cursor.y = saved_y;
            win->cursor.x = saved_x;
        }
    }
}

void mail_apply_tags(const char *args) {
    Buffer *buf = buf_cur();
    if (!buf || !buf->filetype || strcmp(buf->filetype, "mail") != 0) {
        ed_set_status_message("mail-tag: not in a mail list buffer");
        return;
    }
    Window *win = window_cur();
    if (!win) return;

    int row_start = win->cursor.y;
    int row_end   = win->cursor.y;
    if (win->sel.type == SEL_VISUAL ||
        win->sel.type == SEL_VISUAL_LINE ||
        win->sel.type == SEL_VISUAL_BLOCK) {
        int ay = win->sel.anchor_y;
        int cy = win->cursor.y;
        row_start = ay < cy ? ay : cy;
        row_end   = ay > cy ? ay : cy;
    }
    if (row_start < 0) row_start = 0;
    if (row_end >= mail_entry_count) row_end = mail_entry_count - 1;
    if (row_start > row_end) {
        ed_set_status_message("mail-tag: no thread under cursor");
        return;
    }

    char tag_args[512];
    if (parse_tag_args(args, tag_args, sizeof(tag_args)) != 0) return;

    /* Build a thread-id query: thread:a or thread:b or ... */
    char query[4096];
    size_t qout = 0;
    int applied = 0;
    for (int r = row_start; r <= row_end; r++) {
        const char *tid = mail_entries[r].thread_id;
        if (!tid[0]) continue;
        size_t tlen = strlen(tid);
        const char *sep = applied ? " or " : "";
        size_t slen = strlen(sep);
        if (qout + slen + tlen + 1 >= sizeof(query)) {
            ed_set_status_message("mail-tag: too many threads selected");
            return;
        }
        memcpy(query + qout, sep, slen); qout += slen;
        memcpy(query + qout, tid, tlen); qout += tlen;
        applied++;
    }
    query[qout] = '\0';
    if (applied == 0) {
        ed_set_status_message("mail-tag: no threads to tag");
        return;
    }

    char cmd[5120];
    snprintf(cmd, sizeof(cmd),
             "notmuch tag %s -- %s 2>/dev/null", tag_args, query);
    int rc = term_cmd_system(cmd);
    if (rc != 0) {
        ed_set_status_message("mail-tag: notmuch tag exited %d", rc);
        return;
    }

    mail_refresh_keep_cursor();
    ed_set_status_message("mail-tag: %s applied to %d thread%s",
                          tag_args, applied, applied == 1 ? "" : "s");
}

void mail_apply_tags_query(const char *args) {
    Buffer *buf = buf_cur();
    if (!buf || !buf->filetype || strcmp(buf->filetype, "mail") != 0) {
        ed_set_status_message("mail-tag: not in a mail list buffer");
        return;
    }

    char tag_args[512];
    if (parse_tag_args(args, tag_args, sizeof(tag_args)) != 0) return;

    char full_query[1100];
    build_full_query(full_query, sizeof(full_query));

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "notmuch tag %s -- %s 2>/dev/null", tag_args, full_query);
    int rc = term_cmd_system(cmd);
    if (rc != 0) {
        ed_set_status_message("mail-tag: notmuch tag exited %d", rc);
        return;
    }

    mail_refresh_keep_cursor();
    ed_set_status_message("mail-tag: %s applied to all (%s)",
                          tag_args, full_query);
}

/* ------------------------------------------------------------------ */
/* Filter prompt                                                       */
/* ------------------------------------------------------------------ */

static const char *filter_label(Prompt *p) {
    (void)p;
    return "mail filter: ";
}

static void filter_submit(Prompt *p, const char *line, int len) {
    (void)p;
    (void)len;
    mail_set_filter(line);
    mail_open_list();
}

static const PromptVTable filter_vt = {
    .label     = filter_label,
    .on_key    = prompt_default_on_key,
    .on_submit = filter_submit,
};

void mail_filter_prompt(void) {
    prompt_open(&filter_vt, NULL);
}
