#include "buf/row.h"
#include "hed.h"
#include "input/prompt.h"
#include "lib/proc.h"
#include "lib/theme.h"
#include "mail.h"
#include "mail_internal.h"
#include "mail_parse.h"
#include "open/open.h"
#include "select_loop.h"
#include "utils/term_cmd.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Listing cap handed to `notmuch search --limit`. */
#define MAIL_MAX 500
#define MAIL_LIST_BUF "mail://list"
#define MAIL_MBOX_BUF "mail://mailboxes"

/* Attachments of the currently-viewed thread, in "Attachments: [n]"
 * numbering order (stb_ds; taken over from the last MailRender). */
static MailAttachInfo *attachments = NULL;

/* Raw text/html of the viewed thread's newest HTML-bearing message,
 * cached at open like the attachments above. NULL when none. */
static char *view_html = NULL;
static size_t view_html_len = 0;

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    char thread_id[128]; /* "thread:0000000000001234" */
    char display[512];   /* rest of the notmuch summary line */
    int is_unread;       /* 1 if the "unread" tag is present */
} MailEntry;

/* Check for "unread" as a whole word inside the last (...) tag group. */
static int has_unread_tag(const char *line) {
    const char *last_paren = strrchr(line, '(');
    if (!last_paren)
        return 0;
    const char *p = last_paren + 1;
    while (*p && *p != ')') {
        while (*p == ' ')
            p++;
        const char *word = p;
        while (*p && *p != ' ' && *p != ')')
            p++;
        size_t wlen = (size_t)(p - word);
        if (wlen == 6 && memcmp(word, "unread", 6) == 0)
            return 1;
    }
    return 0;
}

static MailEntry *mail_entries = NULL; /* stb_ds */

static char base_query[512] = "*";
static char filter_query[512] = "";
static char mailbox_query[512] = "";
static char sync_cmd[256] = "hml recv";
static char mail_dir[512] = ""; /* lazily initialised to $HOME/.mail */

typedef enum {
    MBE_ALL,     /* "[All mail]" — clears both base and mailbox */
    MBE_VIEW,    /* saved view — sets base_query */
    MBE_MAILBOX, /* account/folder — sets mailbox_query */
    MBE_HEADER,  /* visual separator, not selectable */
} MailboxKind;

typedef struct {
    char display[256];
    char query[256];
    MailboxKind kind;
} MailboxEntry;

static MailboxEntry *mailbox_entries = NULL; /* stb_ds */

typedef struct {
    char name[64];
    char query[256];
} MailView;
static MailView *views = NULL; /* stb_ds */

void mail_add_view(const char *name, const char *query) {
    if (!name || !*name)
        return;
    /* Update or remove existing by name. */
    for (ptrdiff_t i = 0; i < arrlen(views); i++) {
        if (strcmp(views[i].name, name) == 0) {
            if (!query || !*query)
                arrdel(views, i);
            else
                snprintf(views[i].query, sizeof(views[i].query), "%s", query);
            return;
        }
    }
    if (!query || !*query)
        return;
    MailView v;
    snprintf(v.name, sizeof(v.name), "%s", name);
    snprintf(v.query, sizeof(v.query), "%s", query);
    arrput(views, v);
}

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

/* notmuch's `*` is a match-all that can't be combined with AND, so we
 * treat it (and an empty string) as "no constraint" and skip it. */
static int q_is_wild(const char *q) {
    return !q || !q[0] || (q[0] == '*' && q[1] == '\0');
}

static void build_full_query(char *out, size_t sz) {
    out[0] = '\0';
    int n = 0;
    const char *parts[3] = {base_query, mailbox_query, filter_query};
    for (int i = 0; i < 3; i++) {
        if (q_is_wild(parts[i]))
            continue;
        int w =
            snprintf(out + n, sz - n, n == 0 ? "(%s)" : " AND (%s)", parts[i]);
        if (w < 0 || (size_t)w >= sz - n)
            break;
        n += w;
    }
    if (n == 0)
        snprintf(out, sz, "*");
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
    if (mail_dir[0])
        return mail_dir;
    /* "$HOME/.mail", or just ".mail" when HOME is unset. */
    fs_path_home_join(".mail", mail_dir, sizeof(mail_dir));
    return mail_dir;
}

const char *mail_get_dir(void) { return resolve_mail_dir(); }

static int is_maildir(const char *path) {
    char p[1024];
    snprintf(p, sizeof(p), "%s/cur", path);
    if (!fs_is_dir(p))
        return 0;
    snprintf(p, sizeof(p), "%s/new", path);
    if (!fs_is_dir(p))
        return 0;
    return 1;
}

static void mbox_add(const char *display, const char *query, MailboxKind k) {
    MailboxEntry e;
    snprintf(e.display, sizeof(e.display), "%s", display);
    snprintf(e.query, sizeof(e.query), "%s", query ? query : "");
    e.kind = k;
    arrput(mailbox_entries, e);
}

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Sorted subdirectory names (excluding the "cur"/"new"/"tmp" maildir
 * innards) as an stb_ds array of strdup'd strings. */
static char **read_subdirs(const char *path) {
    FsDir *d = NULL;
    if (fs_dir_open(&d, path) != ED_OK)
        return NULL;

    char **names = NULL;
    FsDirEntry de;
    while (fs_dir_next(d, &de)) {
        if (!de.is_dir)
            continue;
        if (!strcmp(de.name, "cur") || !strcmp(de.name, "new") ||
            !strcmp(de.name, "tmp"))
            continue;
        char *dup = strdup(de.name);
        if (dup)
            arrput(names, dup);
    }
    fs_dir_close(d);
    if (arrlen(names) > 1)
        qsort(names, (size_t)arrlen(names), sizeof(*names), cmp_str);
    return names;
}

static void free_names(char **names) {
    for (ptrdiff_t i = 0; i < arrlen(names); i++)
        free(names[i]);
    arrfree(names);
}

/* Scan the maildir root. Two layouts supported:
 *   ~/.mail/cur,new                 — single maildir
 *   ~/.mail/<account>/...           — collection; each account may be a
 *                                     maildir itself or contain folders */
static void mailboxes_scan(void) {
    arr_reset(mailbox_entries);
    mbox_add("[All mail]", "", MBE_ALL);

    if (arrlen(views) > 0) {
        mbox_add("── Views ──", "", MBE_HEADER);
        for (ptrdiff_t i = 0; i < arrlen(views); i++)
            mbox_add(views[i].name, views[i].query, MBE_VIEW);
    }

    const char *root = resolve_mail_dir();

    /* Single top-level maildir. */
    if (is_maildir(root)) {
        mbox_add("── Mailboxes ──", "", MBE_HEADER);
        mbox_add("(root)", "path:**", MBE_MAILBOX);
        return;
    }

    char **accounts = read_subdirs(root);
    if (arrlen(accounts) > 0)
        mbox_add("── Mailboxes ──", "", MBE_HEADER);

    for (ptrdiff_t i = 0; i < arrlen(accounts); i++) {
        const char *acct = accounts[i];
        char acct_path[1024];
        snprintf(acct_path, sizeof(acct_path), "%s/%s", root, acct);

        /* Account-wide entry: path:<acct>/<asterisks>. */
        char qall[256];
        snprintf(qall, sizeof(qall), "path:%s/**", acct);
        mbox_add(acct, qall, MBE_MAILBOX);

        if (is_maildir(acct_path)) {
            char qf[256];
            snprintf(qf, sizeof(qf), "folder:%s", acct);
            mbox_add("  (root)", qf, MBE_MAILBOX);
        }

        char **folders = read_subdirs(acct_path);
        for (ptrdiff_t j = 0; j < arrlen(folders); j++) {
            char child[2048];
            snprintf(child, sizeof(child), "%s/%s", acct_path, folders[j]);
            if (!is_maildir(child))
                continue;
            char qf[256], disp[256];
            snprintf(disp, sizeof(disp), "  %s", folders[j]);
            snprintf(qf, sizeof(qf), "folder:%s/%s", acct, folders[j]);
            mbox_add(disp, qf, MBE_MAILBOX);
        }
        free_names(folders);
    }
    free_names(accounts);
}

/* ------------------------------------------------------------------ */
/* Sync                                                                */
/* ------------------------------------------------------------------ */

void mail_set_sync_cmd(const char *cmd) {
    snprintf(sync_cmd, sizeof(sync_cmd), "%s",
             (cmd && *cmd) ? cmd : "hml recv");
}

/* :mail-sync runs `<sync_cmd> && notmuch new` in a background child so
 * the editor stays responsive; the list refreshes when it exits. */
static struct {
    Proc pr;
    int running;
} sync_job;

static void sync_on_readable(int fd, void *ud) {
    (void)ud;
    char buf[512];
    ssize_t r = read(fd, buf, sizeof(buf));
    if (r > 0)
        return; /* progress chatter — stderr already goes to the log */
    if (r < 0 && (errno == EAGAIN || errno == EINTR))
        return;

    /* EOF: child finished. */
    ed_loop_unregister(fd);
    close(fd);
    sync_job.pr.from_fd = -1;
    int st = 0;
    waitpid(sync_job.pr.pid, &st, 0);
    sync_job.running = 0;

    if (WIFEXITED(st) && WEXITSTATUS(st) == 0) {
        mail_open_list();
        ed_set_status_message("mail: sync complete");
    } else if (WIFEXITED(st)) {
        ed_set_status_message("mail: sync (%s) exited with status %d", sync_cmd,
                              WEXITSTATUS(st));
    } else {
        ed_set_status_message("mail: sync (%s) failed", sync_cmd);
    }
    ed_render_frame();
}

void mail_sync(void) {
    if (sync_job.running) {
        ed_set_status_message("mail: sync already running");
        return;
    }
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "{ %s; } && notmuch new", sync_cmd);
    const char *argv[] = {"sh", "-c", cmd, NULL};
    if (proc_spawn(argv, 0, &sync_job.pr) != 0) {
        ed_set_status_message("mail: failed to run %s", sync_cmd);
        return;
    }
    sync_job.running = 1;
    ed_loop_register("mail-sync", sync_job.pr.from_fd, sync_on_readable, NULL);
    ed_set_status_message("mail: syncing (%s) ...", sync_cmd);
}

/* ------------------------------------------------------------------ */
/* Highlighting                                                        */
/* ------------------------------------------------------------------ */

/* Colors for the mail list — unread entries are bold throughout. */
#define MC_UNREAD_FLAG "\x1b[1;38;2;247;118;142m"    /* bold red            */
#define MC_UNREAD_COUNT "\x1b[1;38;2;86;95;137m"     /* bold muted          */
#define MC_UNREAD_SENDER "\x1b[1;38;2;122;162;247m"  /* bold blue           */
#define MC_UNREAD_SUBJECT "\x1b[1;38;2;192;202;245m" /* bold fg             */
#define MC_READ_FLAG COLOR_COMMENT                   /* dim flag column     */
#define MC_READ_COUNT COLOR_COMMENT                  /* dim [N/M]           */
#define MC_READ_SENDER COLOR_FUNCTION                /* blue                */
#define MC_READ_SUBJECT COLOR_VARIABLE               /* normal fg           */
#define MC_META COLOR_COMMENT                        /* date / tags / dim   */

/* Colors for the mail message view. */
#define MC_MSG_MARKER COLOR_DELIMITER /* section dividers    */
#define MC_MSG_HDR_KEY COLOR_KEYWORD  /* From: / Subject: …  */
#define MC_MSG_HDR_VAL COLOR_VARIABLE /* header value        */
#define MC_MSG_QUOTE COLOR_COMMENT    /* > quoted lines      */

/* A coloured span: [s, e) bytes in the row → SGR escape. */
typedef struct {
    int s, e;
    const char *sgr;
} MailSpan;

/* Build colour spans for one mail-list row.
 * Row format (after our 2-char flag prefix):
 *   [N/M] sender1, sender2; Subject line (relative-date) (tags) */
static int parse_list_spans(const char *raw, int len, MailSpan *sp, int max) {
    int n = 0;
    if (len < 2 || n + 1 > max)
        return 0;

    int unread = (raw[0] == 'U');

    /* 2-char flag column */
    sp[n++] = (MailSpan){0, 2, unread ? MC_UNREAD_FLAG : MC_READ_FLAG};
    int pos = 2;

    /* Thread count [N/M] followed by a space */
    if (pos < len && raw[pos] == '[' && n < max) {
        const char *close = memchr(raw + pos, ']', (size_t)(len - pos));
        if (close) {
            int end = (int)(close - raw) + 2; /* include '] ' */
            if (end > len)
                end = len;
            sp[n++] =
                (MailSpan){pos, end, unread ? MC_UNREAD_COUNT : MC_READ_COUNT};
            pos = end;
        }
    }

    /* Sender: up to and including ';' */
    const char *semi = memchr(raw + pos, ';', (size_t)(len - pos));
    if (semi && n < max) {
        int end = (int)(semi - raw) + 1;
        sp[n++] =
            (MailSpan){pos, end, unread ? MC_UNREAD_SENDER : MC_READ_SENDER};
        pos = end;
        if (pos < len && raw[pos] == ' ')
            pos++;
    }

    /* Subject: everything up to the last '(' (date/tag group) */
    int last_paren = -1;
    for (int i = len - 1; i >= pos; i--) {
        if (raw[i] == '(') {
            last_paren = i;
            break;
        }
    }
    if (last_paren > pos && n < max) {
        sp[n++] = (MailSpan){pos, last_paren,
                             unread ? MC_UNREAD_SUBJECT : MC_READ_SUBJECT};
        pos = last_paren;
    }

    /* Date / tags: remainder */
    if (pos < len && n < max)
        sp[n++] = (MailSpan){pos, len, MC_META};

    return n;
}

static void mail_list_render_hook(const HookRenderEvent *e) {
    if (!e || !e->buf || !e->spans)
        return;
    Buffer *buf = e->buf;
    for (int row = e->row_start; row < e->row_end; row++) {
        if (row < 0 || row >= buf->num_rows)
            continue;
        const char *raw = buf->rows[row].chars.data;
        int len = (int)buf->rows[row].chars.len;
        if (!raw || len <= 0)
            continue;
        MailSpan ms[16];
        int n = parse_list_spans(raw, len, ms, 16);
        for (int i = 0; i < n; i++)
            attrspan_push(e->spans, row, ms[i].s, ms[i].e, ms[i].sgr, 0);
    }
}

/* Known RFC 2822 header names we want to colour. */
static const char *const MAIL_HEADERS[] = {
    "From:",        "To:",         "Cc:",       "Bcc:",
    "Subject:",     "Date:",       "Reply-To:", "Message-Id:",
    "In-Reply-To:", "References:", NULL};

/* Build colour spans for one mail-message row. */
static int parse_msg_spans(const char *raw, int len, MailSpan *sp, int max) {
    int n = 0;
    if (len <= 0)
        return 0;

    /* Section divider emitted by mail_parse between messages — a run
     * of "─" (U+2500, UTF-8 e2 94 80). Match the actual glyph, not
     * just any non-ASCII lead byte, so body text starting with
     * Cyrillic/emoji isn't painted as a divider. */
    if (len >= 3 && (unsigned char)raw[0] == 0xE2 &&
        (unsigned char)raw[1] == 0x94 && (unsigned char)raw[2] == 0x80) {
        if (n < max)
            sp[n++] = (MailSpan){0, len, MC_MSG_MARKER};
        return n;
    }

    /* "Attachments:" pseudo-header gets the same colouring as real headers. */
    if (len > 12 && strncmp(raw, "Attachments:", 12) == 0) {
        if (n + 1 < max) {
            sp[n++] = (MailSpan){0, 12, MC_MSG_HDR_KEY};
            sp[n++] = (MailSpan){12, len, MC_MSG_HDR_VAL};
        }
        return n;
    }

    /* Quoted lines */
    if (raw[0] == '>') {
        if (n < max)
            sp[n++] = (MailSpan){0, len, MC_MSG_QUOTE};
        return n;
    }

    /* RFC 2822 header lines */
    for (int i = 0; MAIL_HEADERS[i]; i++) {
        size_t hlen = strlen(MAIL_HEADERS[i]);
        if ((size_t)len > hlen &&
            strncasecmp(raw, MAIL_HEADERS[i], hlen) == 0) {
            if (n + 1 < max) {
                sp[n++] = (MailSpan){0, (int)hlen, MC_MSG_HDR_KEY};
                sp[n++] = (MailSpan){(int)hlen, len, MC_MSG_HDR_VAL};
            }
            return n;
        }
    }

    return 0; /* body text: no highlight (fall back to plain) */
}

static void mail_msg_render_hook(const HookRenderEvent *e) {
    if (!e || !e->buf || !e->spans)
        return;
    Buffer *buf = e->buf;
    for (int row = e->row_start; row < e->row_end; row++) {
        if (row < 0 || row >= buf->num_rows)
            continue;
        const char *raw = buf->rows[row].chars.data;
        int len = (int)buf->rows[row].chars.len;
        if (!raw || len <= 0)
            continue;
        MailSpan ms[8];
        int n = parse_msg_spans(raw, len, ms, 8);
        for (int i = 0; i < n; i++)
            attrspan_push(e->spans, row, ms[i].s, ms[i].e, ms[i].sgr, 0);
    }
}

/* ------------------------------------------------------------------ */
/* notmuch query → entries                                             */
/* ------------------------------------------------------------------ */

static void mail_run_query(void) {
    char query[1100];
    build_full_query(query, sizeof(query));

    char qq[2200];
    shell_escape_single(query, qq, sizeof(qq));
    char cmd[2400];
    snprintf(cmd, sizeof(cmd),
             "notmuch search --sort=newest-first --limit=%d --output=summary "
             "-- %s 2>/dev/null",
             MAIL_MAX, qq);

    char **lines = NULL;
    int count = 0;
    term_cmd_capture(cmd, &lines, &count);

    arr_reset(mail_entries);
    for (int i = 0; i < count; i++) {
        const char *line = lines[i];
        if (!line || !line[0])
            continue;

        MailEntry e;

        /* First token is the thread ID — ends at the first space. */
        const char *sp = strchr(line, ' ');
        if (sp) {
            size_t tlen = (size_t)(sp - line);
            if (tlen >= sizeof(e.thread_id))
                tlen = sizeof(e.thread_id) - 1;
            memcpy(e.thread_id, line, tlen);
            e.thread_id[tlen] = '\0';
            snprintf(e.display, sizeof(e.display), "%s", sp + 1);
        } else {
            snprintf(e.thread_id, sizeof(e.thread_id), "%s", line);
            e.display[0] = '\0';
        }
        e.is_unread = has_unread_tag(e.display);
        arrput(mail_entries, e);
    }

    term_cmd_free(lines, count);
}

/* ------------------------------------------------------------------ */
/* Open list buffer                                                    */
/* ------------------------------------------------------------------ */

void mail_open_list(void) {
    mail_run_query();
    int n = (int)arrlen(mail_entries);

    /* Highlighting comes from mail_list_render_hook registered in
     * mail_plugin_init; the filetype is the dispatch filter. */
    BufSpecial spec = {.name = MAIL_LIST_BUF,
                       .title = "Mail",
                       .filetype = "mail",
                       .readonly = 1,
                       .as_filename = 1};
    int idx = buf_special_get(&spec, NULL);
    if (idx < 0) {
        ed_set_status_message("mail: failed to open buffer");
        return;
    }

    Buffer *buf = &E.buffers[idx];
    buf_special_clear(buf);

    if (n == 0) {
        buf_special_addf(buf, "(no messages)");
    } else {
        for (int i = 0; i < n; i++)
            buf_special_addf(buf, "%s%s",
                             mail_entries[i].is_unread ? "U " : "  ",
                             mail_entries[i].display);
    }
    buf_special_show(idx);

    int unread = 0;
    for (int i = 0; i < n; i++)
        if (mail_entries[i].is_unread)
            unread++;

    if (n == 0 && mailbox_query[0] && !q_is_wild(base_query)) {
        ed_set_status_message(
            "mail: 0 threads in %s — base query [%s] may be filtering them out "
            "(try :mail-query *)",
            mailbox_query, base_query);
    } else {
        ed_set_status_message(
            "mail: %d threads (%d unread, %d read)  [%s]%s%s%s%s", n, unread,
            n - unread, base_query, mailbox_query[0] ? "  mbox=" : "",
            mailbox_query[0] ? mailbox_query : "",
            filter_query[0] ? "  filter=" : "",
            filter_query[0] ? filter_query : "");
    }
}

/* ------------------------------------------------------------------ */
/* Open thread on <CR>                                                 */
/* ------------------------------------------------------------------ */

/* Drop the "unread" tag (via notmuch) and update the in-memory state +
 * the "U " prefix on the mail-list row. No-op if the thread isn't
 * unread. */
static void mark_thread_read(int row) {
    if (row < 0 || row >= arrlen(mail_entries))
        return;
    if (!mail_entries[row].is_unread)
        return;

    const char *tid = mail_entries[row].thread_id;
    if (!tid[0])
        return;

    char tidq[256];
    shell_escape_single(tid, tidq, sizeof(tidq));
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "notmuch tag -unread -- %s 2>/dev/null", tidq);
    if (term_cmd_system(cmd) != 0)
        return;

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

/* Open (or focus) the thread buffer for `tid` ("thread:…"). `title` is
 * the mail-list display line, or NULL when the thread isn't in the
 * current listing (e.g. followed from a mail:// link) — then the
 * rendered Subject: header stands in. */
static void open_thread_tid(const char *tid, const char *title) {
    if (!tid || !*tid)
        return;

    /* Reuse an already-open thread buffer if present. */
    char bufname[256];
    snprintf(bufname, sizeof(bufname), "mail://%s", tid);

    int existing = buf_find_by_filename(bufname);
    if (existing >= 0) {
        buf_switch(existing);
        if (title)
            ed_set_status_message("%s", title);
        return;
    }

    /* Highlighting via mail_msg_render_hook (registered in
     * mail_plugin_init, filtered on filetype). */
    BufSpecial spec = {.name = bufname,
                       .filetype = "mail-message",
                       .readonly = 1,
                       .as_filename = 1};
    int idx = buf_special_get(&spec, NULL);
    if (idx < 0) {
        ed_set_status_message("mail: failed to open thread buffer");
        return;
    }
    Buffer *tbuf = &E.buffers[idx];

    char tidq[512];
    shell_escape_single(tid, tidq, sizeof(tidq));
    char cmd[600];
    snprintf(cmd, sizeof(cmd),
             "notmuch show --format=text --include-html -- %s 2>/dev/null",
             tidq);

    char **lines = NULL;
    int count = 0;
    term_cmd_capture(cmd, &lines, &count);

    MailRender mr;
    mail_render_init(&mr);
    mail_render_notmuch_text(&mr, lines, count);
    term_cmd_free(lines, count);

    buf_special_clear(tbuf);
    buf_special_add_lines(tbuf, mr.lines, (int)arrlen(mr.lines));

    if (!title) {
        for (ptrdiff_t i = 0; i < arrlen(mr.lines) && i < 20; i++) {
            const char *l = mr.lines[i];
            if (l && strncmp(l, "Subject: ", 9) == 0 && l[9]) {
                title = l + 9;
                break;
            }
        }
    }
    free(tbuf->title);
    tbuf->title = strdup(title ? title : tid);

    /* Take over the attachment list for :mail-attach without
     * rescanning the buffer. */
    arrfree(attachments);
    attachments = mr.attaches;
    mr.attaches = NULL;

    /* Cache the raw HTML for :mail-open-html, stealing it from the
     * render so mail_render_free doesn't drop it. */
    free(view_html);
    view_html = mr.html;
    view_html_len = mr.html_len;
    mr.html = NULL;

    mail_render_free(&mr);

    buf_special_show(idx);

    ed_set_status_message("%s", tbuf->title);
}

static void open_thread_row(int row) {
    if (row < 0 || row >= arrlen(mail_entries))
        return;

    const char *tid = mail_entries[row].thread_id;
    if (!tid[0])
        return;

    mark_thread_read(row);
    open_thread_tid(tid, mail_entries[row].display);
}

void mail_open_thread(const char *tid) {
    if (!tid)
        return;
    if (strncmp(tid, "mail://", 7) == 0)
        tid += 7;
    if (!*tid)
        return;
    /* Prefer the listing row when present so the thread is marked read
     * and the list cursor bookkeeping applies. */
    for (ptrdiff_t i = 0; i < arrlen(mail_entries); i++) {
        if (strcmp(mail_entries[i].thread_id, tid) == 0) {
            open_thread_row((int)i);
            return;
        }
    }
    open_thread_tid(tid, NULL);
}

void mail_handle_enter(void) {
    Buffer *buf = buf_cur();
    if (!buf || !buf->filetype || strcmp(buf->filetype, "mail") != 0)
        return;

    Window *win = window_cur();
    if (!win)
        return;

    int row = win->cursor.y;
    if (row < 0 || row >= arrlen(mail_entries))
        return;

    /* Record current position so <C-o> returns to the mail list. */
    if (buf->filename)
        jump_list_add(&E.jump_list, buf->filename, win->cursor.x,
                      win->cursor.y);

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
    if (!buf || !buf->filename || !buf->filetype)
        return -1;
    if (strcmp(buf->filetype, "mail-message") != 0)
        return -1;
    if (strncmp(buf->filename, "mail://", 7) != 0)
        return -1;
    const char *tid = buf->filename + 7;
    for (ptrdiff_t i = 0; i < arrlen(mail_entries); i++) {
        if (strcmp(mail_entries[i].thread_id, tid) == 0)
            return (int)i;
    }
    return -1;
}

static void goto_message_at(int row) {
    if (row < 0 || row >= arrlen(mail_entries))
        return;
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
    if (r + 1 >= arrlen(mail_entries)) {
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
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;
        const char *tok = p;
        while (*p && *p != ' ' && *p != '\t')
            p++;
        size_t tlen = (size_t)(p - tok);
        if (tlen == 0)
            continue;

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
        if (n)
            out[n++] = ' ';
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
    if (!win)
        return;

    int row_start = win->cursor.y;
    int row_end = win->cursor.y;
    if (win->sel.type == SEL_VISUAL || win->sel.type == SEL_VISUAL_LINE ||
        win->sel.type == SEL_VISUAL_BLOCK) {
        int ay = win->sel.anchor_y;
        int cy = win->cursor.y;
        row_start = ay < cy ? ay : cy;
        row_end = ay > cy ? ay : cy;
    }
    if (row_start < 0)
        row_start = 0;
    if (row_end >= arrlen(mail_entries))
        row_end = (int)arrlen(mail_entries) - 1;
    if (row_start > row_end) {
        ed_set_status_message("mail-tag: no thread under cursor");
        return;
    }

    char tag_args[512];
    if (parse_tag_args(args, tag_args, sizeof(tag_args)) != 0)
        return;

    /* Build a thread-id query: thread:a or thread:b or ... */
    char query[4096];
    size_t qout = 0;
    int applied = 0;
    for (int r = row_start; r <= row_end; r++) {
        const char *tid = mail_entries[r].thread_id;
        if (!tid[0])
            continue;
        size_t tlen = strlen(tid);
        const char *sep = applied ? " or " : "";
        size_t slen = strlen(sep);
        if (qout + slen + tlen + 1 >= sizeof(query)) {
            ed_set_status_message("mail-tag: too many threads selected");
            return;
        }
        memcpy(query + qout, sep, slen);
        qout += slen;
        memcpy(query + qout, tid, tlen);
        qout += tlen;
        applied++;
    }
    query[qout] = '\0';
    if (applied == 0) {
        ed_set_status_message("mail-tag: no threads to tag");
        return;
    }

    char qq[8200];
    shell_escape_single(query, qq, sizeof(qq));
    char cmd[8800];
    snprintf(cmd, sizeof(cmd), "notmuch tag %s -- %s 2>/dev/null", tag_args,
             qq);
    int rc = term_cmd_system(cmd);
    if (rc != 0) {
        ed_set_status_message("mail-tag: notmuch tag exited %d", rc);
        return;
    }

    int was_visual = win->sel.type == SEL_VISUAL ||
                     win->sel.type == SEL_VISUAL_LINE ||
                     win->sel.type == SEL_VISUAL_BLOCK;

    mail_refresh_keep_cursor();

    if (was_visual)
        ed_set_mode(MODE_NORMAL);

    ed_set_status_message("mail-tag: %s applied to %d thread%s", tag_args,
                          applied, applied == 1 ? "" : "s");
}

void mail_apply_tags_query(const char *args) {
    Buffer *buf = buf_cur();
    if (!buf || !buf->filetype || strcmp(buf->filetype, "mail") != 0) {
        ed_set_status_message("mail-tag: not in a mail list buffer");
        return;
    }

    char tag_args[512];
    if (parse_tag_args(args, tag_args, sizeof(tag_args)) != 0)
        return;

    char full_query[1100];
    build_full_query(full_query, sizeof(full_query));

    char qq[2300];
    shell_escape_single(full_query, qq, sizeof(qq));
    char cmd[3000];
    snprintf(cmd, sizeof(cmd), "notmuch tag %s -- %s 2>/dev/null", tag_args,
             qq);
    int rc = term_cmd_system(cmd);
    if (rc != 0) {
        ed_set_status_message("mail-tag: notmuch tag exited %d", rc);
        return;
    }

    mail_refresh_keep_cursor();
    ed_set_status_message("mail-tag: %s applied to all (%s)", tag_args,
                          full_query);
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
    .label = filter_label,
    .on_key = prompt_default_on_key,
    .on_submit = filter_submit,
};

void mail_filter_prompt(void) { prompt_open(&filter_vt, NULL); }

/* ------------------------------------------------------------------ */
/* Mailbox sidebar                                                     */
/* ------------------------------------------------------------------ */

/* Highlight: indented rows (folders) dim, top-level rows bold. */
static void mailbox_render_hook(const HookRenderEvent *ev) {
    if (!ev || !ev->buf || !ev->spans)
        return;
    Buffer *buf = ev->buf;
    for (int row = ev->row_start; row < ev->row_end; row++) {
        if (row < 0 || row >= buf->num_rows)
            continue;
        const char *raw = buf->rows[row].chars.data;
        int len = (int)buf->rows[row].chars.len;
        if (!raw || len <= 0)
            continue;

        int indented = (len >= 2 && raw[0] == ' ' && raw[1] == ' ');
        MailboxKind kind = (row < arrlen(mailbox_entries))
                               ? mailbox_entries[row].kind
                               : MBE_MAILBOX;

        int active = 0;
        if (row < arrlen(mailbox_entries)) {
            const MailboxEntry *e = &mailbox_entries[row];
            if (e->kind == MBE_MAILBOX)
                active = strcmp(e->query, mailbox_query) == 0;
            else if (e->kind == MBE_VIEW)
                active = strcmp(e->query, base_query) == 0;
            else if (e->kind == MBE_ALL)
                active = !mailbox_query[0] && q_is_wild(base_query);
        }

        const char *sgr;
        if (kind == MBE_HEADER)
            sgr = MC_META;
        else if (active)
            sgr = MC_UNREAD_SUBJECT; /* bold fg */
        else if (kind == MBE_ALL)
            sgr = MC_READ_SUBJECT; /* normal  */
        else if (kind == MBE_VIEW)
            sgr = MC_UNREAD_FLAG; /* bold accent */
        else if (indented)
            sgr = MC_META; /* dim     */
        else
            sgr = MC_READ_SENDER; /* blue    */

        attrspan_push(ev->spans, row, 0, len, sgr, 0);
    }
}

void mail_open_mailboxes(void) {
    mailboxes_scan();

    /* Highlighting via mailbox_render_hook (registered in
     * mail_plugin_init, filtered on filetype). */
    BufSpecial spec = {.name = MAIL_MBOX_BUF,
                       .title = "Mailboxes",
                       .filetype = "mail-mailboxes",
                       .readonly = 1,
                       .as_filename = 1};
    int idx = buf_special_get(&spec, NULL);
    if (idx < 0) {
        ed_set_status_message("mail: failed to open mailbox buffer");
        return;
    }

    Buffer *buf = &E.buffers[idx];
    buf_special_clear(buf);
    int active_row = 0;
    if (arrlen(mailbox_entries) == 0) {
        buf_special_addf(buf, "(no mailboxes found — check mail_set_dir)");
    } else {
        for (ptrdiff_t i = 0; i < arrlen(mailbox_entries); i++) {
            const MailboxEntry *e = &mailbox_entries[i];
            buf_special_addf(buf, "%s", e->display);
            if (e->kind == MBE_MAILBOX && strcmp(e->query, mailbox_query) == 0)
                active_row = (int)i;
            else if (e->kind == MBE_VIEW && strcmp(e->query, base_query) == 0)
                active_row = (int)i;
        }
    }
    buf_special_show(idx);
    Window *win = window_cur();
    if (win)
        win->cursor.y = active_row;

    ed_set_status_message("mailboxes: %d entries  (root: %s)",
                          (int)arrlen(mailbox_entries), resolve_mail_dir());
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
            (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_')
            out[n++] = (char)c;
        else
            out[n++] = '_';
    }
    if (n == 0)
        out[n++] = 'x';
    out[n] = '\0';
}

/* Extract one attachment. If dest_dir is NULL, write into /tmp and
 * open with open_path. Otherwise write into dest_dir/<sanitized-name>.
 * Returns 0 on success, non-zero on failure. */
static int extract_attachment(const MailAttachInfo *a, const char *dest_dir) {
    char safe[256];
    sanitize_name(a->filename[0] ? a->filename : "attachment", safe,
                  sizeof(safe));

    char path[1024];
    if (dest_dir) {
        snprintf(path, sizeof(path), "%s/%s", dest_dir, safe);
    } else {
        /* Private temp dir so the real filename (whose extension picks
         * the opener) survives without colliding across instances. */
        char tdir[512];
        if (fs_temp_dir("hed-mail", tdir, sizeof(tdir)) != ED_OK)
            return -1;
        snprintf(path, sizeof(path), "%s/%s", tdir, safe);
    }

    char idq[300];
    snprintf(idq, sizeof(idq), "id:%s", a->msg_id);
    char qq[400];
    shell_escape_single(idq, qq, sizeof(qq));

    char pq[1280];
    shell_escape_single(path, pq, sizeof(pq));

    char cmd[3072];
    snprintf(cmd, sizeof(cmd),
             "notmuch show --part=%d --format=raw -- %s > %s 2>/dev/null",
             a->part_id, qq, pq);
    int rc = term_cmd_system(cmd);
    if (rc != 0)
        return rc;

    if (!dest_dir)
        open_path(path);
    return 0;
}

/* Write the cached HTML body of the viewed message to /tmp and hand it
 * to the system opener (browser). */
void mail_open_html(void) {
    Buffer *buf = buf_cur();
    if (!buf || !buf->filetype || strcmp(buf->filetype, "mail-message") != 0) {
        ed_set_status_message("mail-open-html: open a message first");
        return;
    }
    if (!view_html || view_html_len == 0) {
        ed_set_status_message("mail-open-html: message has no HTML part");
        return;
    }

    /* One live file per editor instance: drop the previous one before
     * writing the next, so repeated invocations don't pile up in /tmp. */
    static int html_seq = 0;
    static char last_path[512];
    static char html_dir[256];
    if (!html_dir[0] &&
        fs_temp_dir("hed-mail-html", html_dir, sizeof(html_dir)) != ED_OK) {
        ed_set_status_message("mail-open-html: temp dir failed");
        return;
    }
    if (last_path[0])
        fs_unlink(last_path);
    char path[512];
    snprintf(path, sizeof(path), "%s/%d.html", html_dir, ++html_seq);
    snprintf(last_path, sizeof(last_path), "%s", path);

    FILE *f = fopen(path, "w");
    if (!f) {
        ed_set_status_message("mail-open-html: cannot write %s", path);
        return;
    }
    size_t wrote = fwrite(view_html, 1, view_html_len, f);
    fclose(f);
    if (wrote != view_html_len) {
        ed_set_status_message("mail-open-html: short write to %s", path);
        return;
    }
    open_path(path);
}

char **mail_extract_attachments_to_tmp(void) {
    Buffer *buf = buf_cur();
    if (!buf || !buf->filetype || strcmp(buf->filetype, "mail-message") != 0)
        return NULL;
    if (arrlen(attachments) == 0)
        return NULL;

    char dir[256];
    if (fs_temp_dir("hed-mail-fwd", dir, sizeof(dir)) != ED_OK)
        return NULL;

    char **paths = NULL;
    for (ptrdiff_t i = 0; i < arrlen(attachments); i++) {
        if (extract_attachment(&attachments[i], dir) != 0)
            continue;
        char safe[256];
        sanitize_name(attachments[i].filename[0] ? attachments[i].filename
                                                 : "attachment",
                      safe, sizeof(safe));
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, safe);
        char *dup = strdup(path);
        if (dup)
            arrput(paths, dup);
    }
    return paths;
}

/* Run the picked attachments through extract_attachment. Reports
 * aggregate status to the status line. */
static void act_on_attachments(const MailAttachInfo **picks, int n,
                               const char *dest_dir) {
    int ok = 0, fail = 0;
    for (int i = 0; i < n; i++) {
        if (extract_attachment(picks[i], dest_dir) == 0)
            ok++;
        else
            fail++;
    }
    if (dest_dir) {
        if (fail == 0)
            ed_set_status_message("mail-attach: saved %d to %s", ok, dest_dir);
        else
            ed_set_status_message("mail-attach: saved %d, %d failed (dir %s)",
                                  ok, fail, dest_dir);
    } else if (fail > 0) {
        ed_set_status_message("mail-attach: opened %d, %d failed", ok, fail);
    }
}

void mail_attach_action(int att_no, const char *dest_dir) {
    Buffer *buf = buf_cur();
    if (!buf || !buf->filetype || strcmp(buf->filetype, "mail-message") != 0) {
        ed_set_status_message("mail-attach: open a message first");
        return;
    }

    int n_att = (int)arrlen(attachments);
    if (n_att == 0) {
        ed_set_status_message("mail-attach: no attachments in this message");
        return;
    }

    /* Resolve / create dest dir up front so we fail fast. */
    char *resolved_dir = NULL;
    if (dest_dir && *dest_dir) {
        char full[PATH_MAX];
        str_expand_tilde(dest_dir, full, sizeof(full));
        resolved_dir = strdup(full);
        if (!resolved_dir) {
            ed_set_status_message("mail-attach: out of memory");
            return;
        }
        /* Trim a single trailing slash so "%s/%s" stays clean. */
        size_t L = strlen(resolved_dir);
        while (L > 1 && resolved_dir[L - 1] == '/')
            resolved_dir[--L] = '\0';
        if (fs_mkdir_p(resolved_dir) != ED_OK) {
            ed_set_status_message("mail-attach: cannot create dir %s",
                                  resolved_dir);
            free(resolved_dir);
            return;
        }
    }

    /* Direct path: act on one attachment by its 1-based number (as
     * shown in the rendered Attachments: line), no fzf. Numbers are
     * thread-wide, so they stay unambiguous even when several
     * messages reuse the same notmuch part id. */
    if (att_no >= 0) {
        if (att_no < 1 || att_no > n_att) {
            ed_set_status_message("mail-attach: no attachment %d (1..%d)",
                                  att_no, n_att);
            free(resolved_dir);
            return;
        }
        const MailAttachInfo *one[1] = {&attachments[att_no - 1]};
        act_on_attachments(one, 1, resolved_dir);
        free(resolved_dir);
        return;
    }

    /* Auto-pick when there is only one attachment. */
    if (n_att == 1) {
        const MailAttachInfo *one[1] = {&attachments[0]};
        act_on_attachments(one, 1, resolved_dir);
        free(resolved_dir);
        return;
    }

    /* Multiple attachments: fzf with multi=1. Tab toggles, <C-a>
     * select-all. Items are "[<number>] <filename>". */
    const char **items = malloc(sizeof(char *) * (size_t)n_att);
    char **labels = malloc(sizeof(char *) * (size_t)n_att);
    if (!items || !labels) {
        free(items);
        free(labels);
        free(resolved_dir);
        ed_set_status_message("mail-attach: out of memory");
        return;
    }
    for (int i = 0; i < n_att; i++) {
        char tmp[320];
        snprintf(tmp, sizeof(tmp), "[%d] %s", i + 1,
                 attachments[i].filename[0] ? attachments[i].filename
                                            : "(unnamed)");
        labels[i] = strdup(tmp);
        items[i] = labels[i];
    }
    char **sel = NULL;
    int cnt = 0;
    int ok = picker_list(items, n_att, 1, &sel, &cnt);
    for (int i = 0; i < n_att; i++)
        free(labels[i]);
    free(labels);
    free(items);

    if (!ok || cnt <= 0) {
        picker_list_free(sel, cnt);
        ed_set_status_message("mail-attach: canceled");
        free(resolved_dir);
        return;
    }

    const MailAttachInfo **picks =
        malloc(sizeof(MailAttachInfo *) * (size_t)cnt);
    if (!picks) {
        picker_list_free(sel, cnt);
        free(resolved_dir);
        ed_set_status_message("mail-attach: out of memory");
        return;
    }
    int picked = 0;
    for (int i = 0; i < cnt; i++) {
        int no = 0;
        if (!sel[i] || sscanf(sel[i], "[%d]", &no) != 1 || no < 1 || no > n_att)
            continue;
        picks[picked++] = &attachments[no - 1];
    }
    picker_list_free(sel, cnt);

    if (picked == 0) {
        ed_set_status_message("mail-attach: nothing matched selection");
    } else {
        act_on_attachments(picks, picked, resolved_dir);
    }
    free(picks);
    free(resolved_dir);
}

void mail_handle_mailbox_enter(void) {
    Buffer *buf = buf_cur();
    if (!buf || !buf->filetype || strcmp(buf->filetype, "mail-mailboxes") != 0)
        return;

    Window *win = window_cur();
    if (!win)
        return;
    int row = win->cursor.y;
    if (row < 0 || row >= arrlen(mailbox_entries))
        return;

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

void mail_register_render_hooks(void) {
    hook_register_render(HOOK_RENDER_PRE, -1, "mail", mail_list_render_hook);
    hook_register_render(HOOK_RENDER_PRE, -1, "mail-message",
                         mail_msg_render_hook);
    /* Same shape as mail-message — composing a reply or new message
     * gets the header/quote coloring while editing. */
    hook_register_render(HOOK_RENDER_PRE, -1, "mail-compose",
                         mail_msg_render_hook);
    hook_register_render(HOOK_RENDER_PRE, -1, "mail-mailboxes",
                         mailbox_render_hook);
}
