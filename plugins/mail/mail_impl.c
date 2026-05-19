#include "mail.h"
#include "mail_parse.h"
#include "hed.h"
#include "buf/row.h"
#include "lib/theme.h"
#include "open/open.h"
#include "prompt.h"
#include "utils/term_cmd.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MAIL_MAX        500
#define MAIL_MBOX_MAX   256
#define MAIL_ATTACH_MAX 32
#define MAIL_LIST_BUF   "mail://list"
#define MAIL_MBOX_BUF   "mail://mailboxes"

typedef struct {
    char msg_id[256];
    int  part_id;
    char filename[256];
} MailAttach;

static MailAttach attachments[MAIL_ATTACH_MAX];
static int        attach_count = 0;

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

static char base_query[512]     = "*";
static char filter_query[512]   = "";
static char mailbox_query[512]  = "";
static char mbsync_profile[128] = "-a";
static char mail_dir[512]       = "";  /* lazily initialised to $HOME/.mail */

typedef enum {
    MBE_ALL,      /* "[All mail]" — clears both base and mailbox */
    MBE_VIEW,     /* saved view — sets base_query */
    MBE_MAILBOX,  /* account/folder — sets mailbox_query */
    MBE_HEADER,   /* visual separator, not selectable */
} MailboxKind;

typedef struct {
    char        display[256];
    char        query[256];
    MailboxKind kind;
} MailboxEntry;

static MailboxEntry mailbox_entries[MAIL_MBOX_MAX];
static int          mailbox_entry_count = 0;

#define MAIL_VIEWS_MAX 32
typedef struct {
    char name[64];
    char query[256];
} MailView;
static MailView views[MAIL_VIEWS_MAX];
static int      view_count = 0;

void mail_add_view(const char *name, const char *query) {
    if (!name || !*name) return;
    /* Update or remove existing by name. */
    for (int i = 0; i < view_count; i++) {
        if (strcmp(views[i].name, name) == 0) {
            if (!query || !*query) {
                views[i] = views[--view_count]; /* unordered remove */
            } else {
                snprintf(views[i].query, sizeof(views[i].query), "%s", query);
            }
            return;
        }
    }
    if (!query || !*query) return;
    if (view_count >= MAIL_VIEWS_MAX) return;
    MailView *v = &views[view_count++];
    snprintf(v->name,  sizeof(v->name),  "%s", name);
    snprintf(v->query, sizeof(v->query), "%s", query);
}

/* Forward-declared internal helper from buf/row.c */
void buf_row_insert_in(Buffer *buf, int at, const char *s, size_t len);

/* ------------------------------------------------------------------ */
/* Query helpers                                                       */
/* ------------------------------------------------------------------ */

void mail_set_query(const char *q) {
    snprintf(base_query, sizeof(base_query), "%s", q && *q ? q : "*");
}

const char *mail_get_query(void) { return base_query; }

void mail_set_filter(const char *f) {
    snprintf(filter_query, sizeof(filter_query), "%s", f ? f : "");
}

/* Wrap `in` in single quotes for /bin/sh, escaping embedded quotes.
 * Required for any notmuch query passed through popen because `*`,
 * wildcards, and parentheses are otherwise shell-expanded. */
static void shell_quote(const char *in, char *out, size_t cap) {
    size_t n = 0;
    if (cap < 3) { if (cap) out[0] = '\0'; return; }
    out[n++] = '\'';
    for (const char *p = in; *p && n + 5 < cap; p++) {
        if (*p == '\'') {
            if (n + 5 >= cap) break;
            memcpy(out + n, "'\\''", 4);
            n += 4;
        } else {
            out[n++] = *p;
        }
    }
    out[n++] = '\'';
    out[n]   = '\0';
}

/* notmuch's `*` is a match-all that can't be combined with AND, so we
 * treat it (and an empty string) as "no constraint" and skip it. */
static int q_is_wild(const char *q) {
    return !q || !q[0] || (q[0] == '*' && q[1] == '\0');
}

static void build_full_query(char *out, size_t sz) {
    out[0] = '\0';
    int n = 0;
    const char *parts[3] = { base_query, mailbox_query, filter_query };
    for (int i = 0; i < 3; i++) {
        if (q_is_wild(parts[i])) continue;
        int w = snprintf(out + n, sz - n,
                         n == 0 ? "(%s)" : " AND (%s)", parts[i]);
        if (w < 0 || (size_t)w >= sz - n) break;
        n += w;
    }
    if (n == 0) snprintf(out, sz, "*");
}

void mail_set_mailbox(const char *q) {
    snprintf(mailbox_query, sizeof(mailbox_query), "%s", q ? q : "");
}

const char *mail_get_mailbox(void) { return mailbox_query; }

/* ------------------------------------------------------------------ */
/* Mail dir + mailbox discovery                                        */
/* ------------------------------------------------------------------ */

void mail_set_dir(const char *dir) {
    snprintf(mail_dir, sizeof(mail_dir), "%s", dir ? dir : "");
}

static const char *resolve_mail_dir(void) {
    if (mail_dir[0]) return mail_dir;
    const char *home = getenv("HOME");
    if (home && *home)
        snprintf(mail_dir, sizeof(mail_dir), "%s/.mail", home);
    else
        snprintf(mail_dir, sizeof(mail_dir), ".mail");
    return mail_dir;
}

const char *mail_get_dir(void) { return resolve_mail_dir(); }

static int is_maildir(const char *path) {
    char p[1024];
    struct stat st;
    snprintf(p, sizeof(p), "%s/cur", path);
    if (stat(p, &st) != 0 || !S_ISDIR(st.st_mode)) return 0;
    snprintf(p, sizeof(p), "%s/new", path);
    if (stat(p, &st) != 0 || !S_ISDIR(st.st_mode)) return 0;
    return 1;
}

/* Append an entry, bounds-checked. */
static void mbox_add(const char *display, const char *query, MailboxKind k) {
    if (mailbox_entry_count >= MAIL_MBOX_MAX) return;
    MailboxEntry *e = &mailbox_entries[mailbox_entry_count++];
    snprintf(e->display, sizeof(e->display), "%s", display);
    snprintf(e->query,   sizeof(e->query),   "%s", query ? query : "");
    e->kind = k;
}

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Read sorted directory entries (excluding "." / ".." and hidden helpers
 * like "cur" / "new" / "tmp"). Caller frees each entry and the array. */
static int read_subdirs(const char *path, char ***out) {
    DIR *d = opendir(path);
    if (!d) { *out = NULL; return 0; }

    char **names = NULL;
    int    cap   = 0;
    int    n     = 0;
    struct dirent *de;
    while ((de = readdir(d))) {
        const char *name = de->d_name;
        if (name[0] == '.') continue;
        if (!strcmp(name, "cur") || !strcmp(name, "new") ||
            !strcmp(name, "tmp")) continue;

        char child[1024];
        snprintf(child, sizeof(child), "%s/%s", path, name);
        struct stat st;
        if (stat(child, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        if (n == cap) {
            cap = cap ? cap * 2 : 16;
            char **nn = realloc(names, (size_t)cap * sizeof(*nn));
            if (!nn) break;
            names = nn;
        }
        names[n++] = strdup(name);
    }
    closedir(d);
    if (n > 1) qsort(names, (size_t)n, sizeof(*names), cmp_str);
    *out = names;
    return n;
}

static void free_names(char **names, int n) {
    for (int i = 0; i < n; i++) free(names[i]);
    free(names);
}

/* Scan the maildir root. Two layouts supported:
 *   ~/.mail/cur,new                 — single maildir
 *   ~/.mail/<account>/...           — collection; each account may be a
 *                                     maildir itself or contain folders */
static void mailboxes_scan(void) {
    mailbox_entry_count = 0;
    mbox_add("[All mail]", "", MBE_ALL);

    if (view_count > 0) {
        mbox_add("── Views ──", "", MBE_HEADER);
        for (int i = 0; i < view_count; i++)
            mbox_add(views[i].name, views[i].query, MBE_VIEW);
    }

    const char *root = resolve_mail_dir();

    /* Single top-level maildir. */
    if (is_maildir(root)) {
        mbox_add("── Mailboxes ──", "", MBE_HEADER);
        mbox_add("(root)", "path:**", MBE_MAILBOX);
        return;
    }

    char **accounts = NULL;
    int    nacc     = read_subdirs(root, &accounts);
    if (nacc > 0) mbox_add("── Mailboxes ──", "", MBE_HEADER);

    for (int i = 0; i < nacc; i++) {
        const char *acct = accounts[i];
        char acct_path[1024];
        snprintf(acct_path, sizeof(acct_path), "%s/%s", root, acct);

        /* Account-wide entry: path:<acct>/<asterisks>. */
        char qall[256];
        snprintf(qall, sizeof(qall), "path:%s/**", acct);
        mbox_add(acct, qall, MBE_MAILBOX);

        if (is_maildir(acct_path)) {
            char qf[256], disp[256];
            snprintf(disp, sizeof(disp), "  (root)");
            snprintf(qf, sizeof(qf), "folder:%s", acct);
            mbox_add(disp, qf, MBE_MAILBOX);
        }

        char **folders = NULL;
        int    nf      = read_subdirs(acct_path, &folders);
        for (int j = 0; j < nf; j++) {
            char child[2048];
            snprintf(child, sizeof(child), "%s/%s", acct_path, folders[j]);
            if (!is_maildir(child)) continue;
            char qf[256], disp[256];
            snprintf(disp, sizeof(disp), "  %s", folders[j]);
            snprintf(qf, sizeof(qf), "folder:%s/%s", acct, folders[j]);
            mbox_add(disp, qf, MBE_MAILBOX);
        }
        free_names(folders, nf);
    }
    free_names(accounts, nacc);
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

    /* Section separator emitted by mail_parse between messages. */
    if ((unsigned char)raw[0] >= 0x80) {
        if (n < max) sp[n++] = (MailSpan){ 0, len, MC_MSG_MARKER };
        return n;
    }

    /* "Attachments:" pseudo-header gets the same colouring as real headers. */
    if (len > 12 && strncmp(raw, "Attachments:", 12) == 0) {
        if (n + 1 < max) {
            sp[n++] = (MailSpan){ 0,  12,  MC_MSG_HDR_KEY };
            sp[n++] = (MailSpan){ 12, len, MC_MSG_HDR_VAL };
        }
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

size_t mail_msg_hl(Buffer *buf, int row,
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

    char qq[2200];
    shell_quote(query, qq, sizeof(qq));
    char cmd[2400];
    snprintf(cmd, sizeof(cmd),
             "notmuch search --sort=newest-first --limit=500 --output=summary -- %s 2>/dev/null", qq);

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

    if (mail_entry_count == 0 && mailbox_query[0] && !q_is_wild(base_query)) {
        ed_set_status_message(
            "mail: 0 threads in %s — base query [%s] may be filtering them out "
            "(try :mail-query *)",
            mailbox_query, base_query);
    } else {
        ed_set_status_message(
            "mail: %d threads (%d unread, %d read)  [%s]%s%s%s%s",
            mail_entry_count, unread, read, base_query,
            mailbox_query[0] ? "  mbox=" : "", mailbox_query[0] ? mailbox_query : "",
            filter_query[0]  ? "  filter=" : "", filter_query[0]  ? filter_query  : "");
    }
}

/* ------------------------------------------------------------------ */
/* Open thread on <CR>                                                 */
/* ------------------------------------------------------------------ */

/* Drop the "unread" tag (via notmuch) and update the in-memory state +
 * the "U " prefix on the mail-list row. No-op if the thread isn't
 * unread. */
static void mark_thread_read(int row) {
    if (row < 0 || row >= mail_entry_count) return;
    if (!mail_entries[row].is_unread) return;

    const char *tid = mail_entries[row].thread_id;
    if (!tid[0]) return;

    char tidq[256];
    shell_quote(tid, tidq, sizeof(tidq));
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "notmuch tag -unread -- %s 2>/dev/null", tidq);
    if (term_cmd_system(cmd) != 0) return;

    mail_entries[row].is_unread = 0;

    int lidx = buf_find_by_filename(MAIL_LIST_BUF);
    if (lidx >= 0) {
        Buffer *lb = &E.buffers[lidx];
        if (row < lb->num_rows && lb->rows[row].chars.len >= 1 &&
            lb->rows[row].chars.data[0] == 'U') {
            lb->rows[row].chars.data[0] = ' ';
            buf_row_update(&lb->rows[row]);
        }
    }
}

/* Open (or focus) the thread for mail_entries[row], marking it as read.
 * The caller is responsible for jump-list bookkeeping and for syncing
 * the mail-list buffer's cursor if needed. */
static void open_thread_row(int row) {
    if (row < 0 || row >= mail_entry_count) return;

    const char *tid = mail_entries[row].thread_id;
    if (!tid[0]) return;

    mark_thread_read(row);

    /* Reuse an already-open thread buffer if present. */
    char bufname[256];
    snprintf(bufname, sizeof(bufname), "mail://%s", tid);

    int existing = buf_find_by_filename(bufname);
    if (existing >= 0) {
        buf_switch(existing);
        ed_set_status_message("%s", mail_entries[row].display);
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
             "notmuch show --format=text --include-html -- %s 2>/dev/null", tid);

    char **lines = NULL;
    int    count = 0;
    term_cmd_capture(cmd, &lines, &count);

    MailRender mr;
    mail_render_init(&mr);
    mail_render_notmuch_text(&mr, lines, count);
    term_cmd_free(lines, count);

    clear_buffer(tbuf);
    for (int i = 0; i < mr.line_count; i++) {
        const char *l = mr.lines[i] ? mr.lines[i] : "";
        buf_row_insert_in(tbuf, tbuf->num_rows, l, strlen(l));
    }

    /* Cache attachments for :mail-attach without rescanning the buffer. */
    attach_count = 0;
    for (int i = 0; i < mr.attach_count && attach_count < MAIL_ATTACH_MAX; i++) {
        MailAttach *a = &attachments[attach_count++];
        a->part_id = mr.attaches[i].part_id;
        snprintf(a->msg_id,   sizeof(a->msg_id),   "%s", mr.attaches[i].msg_id);
        snprintf(a->filename, sizeof(a->filename), "%s", mr.attaches[i].filename);
    }
    mail_render_free(&mr);

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

void mail_handle_enter(void) {
    Buffer *buf = buf_cur();
    if (!buf || !buf->filetype || strcmp(buf->filetype, "mail") != 0) return;

    Window *win = window_cur();
    if (!win) return;

    int row = win->cursor.y;
    if (row < 0 || row >= mail_entry_count) return;

    /* Record current position so <C-o> returns to the mail list. */
    if (buf->filename)
        jump_list_add(&E.jump_list, buf->filename, win->cursor.x, win->cursor.y);

    /* Persist the mail-list cursor onto the buffer so closing the thread
     * buffer (which restores from buf->cursor) returns us to this row. */
    if (buf->cursor) {
        buf->cursor->x = win->cursor.x;
        buf->cursor->y = win->cursor.y;
    }

    open_thread_row(row);
}

/* Find the row in mail_entries for the message currently displayed in
 * the focused window (filetype "mail-message", filename "mail://<tid>").
 * Returns -1 if not viewing a mail message or the tid isn't in the
 * current listing. */
static int find_current_message_row(void) {
    Buffer *buf = buf_cur();
    if (!buf || !buf->filename || !buf->filetype) return -1;
    if (strcmp(buf->filetype, "mail-message") != 0) return -1;
    if (strncmp(buf->filename, "mail://", 7) != 0) return -1;
    const char *tid = buf->filename + 7;
    for (int i = 0; i < mail_entry_count; i++) {
        if (strcmp(mail_entries[i].thread_id, tid) == 0) return i;
    }
    return -1;
}

static void goto_message_at(int row) {
    if (row < 0 || row >= mail_entry_count) return;
    /* Keep the mail-list cursor in sync so closing the message buffer
     * later returns the user to the right row. */
    int lidx = buf_find_by_filename(MAIL_LIST_BUF);
    if (lidx >= 0 && E.buffers[lidx].cursor) {
        E.buffers[lidx].cursor->y = row;
        E.buffers[lidx].cursor->x = 0;
    }
    open_thread_row(row);
}

void mail_next_message(void) {
    int r = find_current_message_row();
    if (r < 0) {
        ed_set_status_message("mail: not viewing a mail message");
        return;
    }
    if (r + 1 >= mail_entry_count) {
        ed_set_status_message("mail: no next message");
        return;
    }
    goto_message_at(r + 1);
}

void mail_prev_message(void) {
    int r = find_current_message_row();
    if (r < 0) {
        ed_set_status_message("mail: not viewing a mail message");
        return;
    }
    if (r <= 0) {
        ed_set_status_message("mail: no previous message");
        return;
    }
    goto_message_at(r - 1);
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

    char qq[8200];
    shell_quote(query, qq, sizeof(qq));
    char cmd[8800];
    snprintf(cmd, sizeof(cmd),
             "notmuch tag %s -- %s 2>/dev/null", tag_args, qq);
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

    char qq[2300];
    shell_quote(full_query, qq, sizeof(qq));
    char cmd[3000];
    snprintf(cmd, sizeof(cmd),
             "notmuch tag %s -- %s 2>/dev/null", tag_args, qq);
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

/* ------------------------------------------------------------------ */
/* Mailbox sidebar                                                     */
/* ------------------------------------------------------------------ */

/* Highlight: indented rows (folders) dim, top-level rows bold. */
static size_t mailbox_hl(Buffer *buf, int row,
                         char *dst, size_t dst_cap,
                         int col_off, int max_cols) {
    if (row < 0 || row >= buf->num_rows) return 0;
    const char *raw = buf->rows[row].render.data;
    int         len = (int)buf->rows[row].render.len;
    if (!raw || len <= 0) return 0;

    int indented = (len >= 2 && raw[0] == ' ' && raw[1] == ' ');
    MailboxKind kind = (row < mailbox_entry_count)
                           ? mailbox_entries[row].kind : MBE_MAILBOX;

    int active = 0;
    if (row < mailbox_entry_count) {
        const MailboxEntry *e = &mailbox_entries[row];
        if      (e->kind == MBE_MAILBOX) active = strcmp(e->query, mailbox_query) == 0;
        else if (e->kind == MBE_VIEW)    active = strcmp(e->query, base_query)    == 0;
        else if (e->kind == MBE_ALL)     active = !mailbox_query[0] && q_is_wild(base_query);
    }

    MailSpan spans[2];
    int n = 0;
    const char *sgr;
    if (kind == MBE_HEADER) sgr = MC_META;
    else if (active)        sgr = MC_UNREAD_SUBJECT;     /* bold fg */
    else if (kind == MBE_ALL)  sgr = MC_READ_SUBJECT;    /* normal  */
    else if (kind == MBE_VIEW) sgr = MC_UNREAD_FLAG;     /* bold accent */
    else if (indented)         sgr = MC_META;            /* dim     */
    else                       sgr = MC_READ_SENDER;     /* blue    */
    spans[n++] = (MailSpan){ 0, len, sgr };

    return emit_spans(raw, len, spans, n, dst, dst_cap, col_off, max_cols);
}

void mail_open_mailboxes(void) {
    mailboxes_scan();

    int idx = buf_find_by_filename(MAIL_MBOX_BUF);
    if (idx < 0) {
        if (buf_new(MAIL_MBOX_BUF, &idx) != ED_OK) {
            ed_set_status_message("mail: failed to open mailbox buffer");
            return;
        }
    } else {
        buf_switch(idx);
    }

    Buffer *buf = &E.buffers[idx];
    free(buf->title);    buf->title    = strdup("Mailboxes");
    free(buf->filetype); buf->filetype = strdup("mail-mailboxes");
    buf->readonly   = 1;
    buf->hl_line_fn = mailbox_hl;

    clear_buffer(buf);
    if (mailbox_entry_count == 0) {
        const char *msg = "(no mailboxes found — check mail_set_dir)";
        buf_row_insert_in(buf, 0, msg, strlen(msg));
    } else {
        int active_row = 0;
        for (int i = 0; i < mailbox_entry_count; i++) {
            const MailboxEntry *e = &mailbox_entries[i];
            buf_row_insert_in(buf, buf->num_rows, e->display, strlen(e->display));
            if (e->kind == MBE_MAILBOX && strcmp(e->query, mailbox_query) == 0)
                active_row = i;
            else if (e->kind == MBE_VIEW && strcmp(e->query, base_query) == 0)
                active_row = i;
        }
        Window *win = window_cur();
        if (win) {
            win_attach_buf(win, buf);
            win->cursor.x = 0;
            win->cursor.y = active_row;
        }
    }
    buf->dirty = 0;
    E.current_buffer = idx;

    ed_set_status_message("mailboxes: %d entries  (root: %s)",
                          mailbox_entry_count, resolve_mail_dir());
}

/* ------------------------------------------------------------------ */
/* Attachments                                                         */
/* ------------------------------------------------------------------ */

/* Sanitize a filename for safe use in /tmp paths — drop everything
 * that isn't alnum, dot, dash, or underscore. */
static void sanitize_name(const char *in, char *out, size_t cap) {
    size_t n = 0;
    for (const char *p = in; *p && n + 1 < cap; p++) {
        unsigned char c = (unsigned char)*p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '.' || c == '-' || c == '_')
            out[n++] = (char)c;
        else
            out[n++] = '_';
    }
    if (n == 0) out[n++] = 'x';
    out[n] = '\0';
}

static void extract_and_open(const MailAttach *a) {
    char safe[256];
    sanitize_name(a->filename[0] ? a->filename : "attachment", safe, sizeof(safe));
    char path[512];
    snprintf(path, sizeof(path), "/tmp/hed-mail-%d-%s", a->part_id, safe);

    char idq[300];
    snprintf(idq, sizeof(idq), "id:%s", a->msg_id);
    char qq[400];
    shell_quote(idq, qq, sizeof(qq));

    char pq[1024];
    shell_quote(path, pq, sizeof(pq));

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "notmuch show --part=%d --format=raw -- %s > %s 2>/dev/null",
             a->part_id, qq, pq);
    int rc = term_cmd_system(cmd);
    if (rc != 0) {
        ed_set_status_message("mail-attach: extract failed (status %d)", rc);
        return;
    }
    open_path(path);
}

void mail_open_attachment(int part_id) {
    Buffer *buf = buf_cur();
    if (!buf || !buf->filetype ||
        strcmp(buf->filetype, "mail-message") != 0) {
        ed_set_status_message("mail-attach: open a message first");
        return;
    }

    if (attach_count == 0) {
        ed_set_status_message("mail-attach: no attachments in this message");
        return;
    }

    if (part_id < 0) {
        if (attach_count == 1) {
            extract_and_open(&attachments[0]);
            return;
        }
        char list[512];
        size_t n = 0;
        for (int i = 0; i < attach_count && n + 32 < sizeof(list); i++) {
            n += (size_t)snprintf(list + n, sizeof(list) - n,
                                  "%s[%d] %s",
                                  i ? "  " : "",
                                  attachments[i].part_id,
                                  attachments[i].filename);
        }
        ed_set_status_message(
            "mail-attach: %d attachments — :mail-attach <id> to open. %s",
            attach_count, list);
        return;
    }

    for (int i = 0; i < attach_count; i++) {
        if (attachments[i].part_id == part_id) {
            extract_and_open(&attachments[i]);
            return;
        }
    }
    ed_set_status_message("mail-attach: no attachment with part id %d", part_id);
}

void mail_handle_mailbox_enter(void) {
    Buffer *buf = buf_cur();
    if (!buf || !buf->filetype ||
        strcmp(buf->filetype, "mail-mailboxes") != 0) return;

    Window *win = window_cur();
    if (!win) return;
    int row = win->cursor.y;
    if (row < 0 || row >= mailbox_entry_count) return;

    MailboxEntry *e = &mailbox_entries[row];
    switch (e->kind) {
    case MBE_HEADER:
        return;
    case MBE_ALL:
        mail_set_mailbox("");
        mail_set_query("*");
        break;
    case MBE_VIEW:
        mail_set_query(e->query);
        break;
    case MBE_MAILBOX:
        mail_set_mailbox(e->query);
        break;
    }
    mail_open_list();
}
