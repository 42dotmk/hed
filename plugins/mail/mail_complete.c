/* Address completion in compose buffers: a completion source that is
 * live on the To:/Cc:/Bcc: headers and asks `hml address` for the
 * correspondents matching what has been typed since the last comma.
 * The query runs async (a word prefix over the whole index can take a
 * few hundred ms); a newer request kills the one in flight. */

#include "completion/completion.h"
#include "hed.h"
#include "lib/proc.h"
#include "mail_internal.h"
#include "select_loop.h"
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>

#define ADDR_LIMIT "--limit=50"

static struct {
    Proc pr;
    unsigned token;
    int start; /* byte col the accepted address replaces from */
    StrBuf out;
} job = {.pr = {.pid = 0, .to_fd = -1, .from_fd = -1}};

static void job_stop(int sig) {
    if (job.pr.from_fd >= 0) {
        ed_loop_unregister(job.pr.from_fd);
        close(job.pr.from_fd);
        job.pr.from_fd = -1;
    }
    if (job.pr.pid > 0) {
        if (sig)
            kill(job.pr.pid, sig);
        waitpid(job.pr.pid, NULL, 0);
        job.pr.pid = 0;
    }
}

/* One `hml address` line → item: the display name as the label, the
 * bare address as the detail, the whole mailbox inserted. Filtering runs
 * on the lowercased mailbox, so the typed word may hit either part and
 * every candidate ranks alike on case — hml's order then decides. */
static int mailbox_item(const char *line, size_t len, int order, CmpItem *it) {
    *it = (CmpItem){
        .kind = CMP_KIND_OTHER, .edit_start = job.start, .edit_end = -1};
    const char *lt = memchr(line, '<', len);
    const char *gt = lt ? memchr(lt, '>', len - (size_t)(lt - line)) : NULL;
    if (lt && gt && lt > line) {
        const char *ns = line, *ne = lt;
        while (ne > ns && (ne[-1] == ' ' || ne[-1] == '"'))
            ne--;
        while (ns < ne && *ns == '"')
            ns++;
        it->label = strndup(ns, (size_t)(ne - ns));
        it->detail = strndup(lt + 1, (size_t)(gt - lt - 1));
        it->insert_text = strndup(line, len);
    } else {
        it->label = strndup(line, len);
    }
    it->filter_text = strndup(line, len);
    it->sort_text = malloc(12);
    if (!it->label || !it->filter_text || !it->sort_text ||
        (lt && gt && lt > line && (!it->detail || !it->insert_text))) {
        free(it->label);
        free(it->detail);
        free(it->insert_text);
        free(it->filter_text);
        free(it->sort_text);
        return -1;
    }
    for (char *c = it->filter_text; *c; c++)
        *c = (char)tolower((unsigned char)*c);
    snprintf(it->sort_text, 12, "%06d", order);
    return 0;
}

static void job_deliver(void) {
    CmpItem *items = NULL;
    int n = 0, cap = 0;
    const char *p = job.out.data, *end = p ? p + job.out.len : NULL;

    while (p && p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
        if (len > 0) {
            if (n == cap) {
                cap = cap ? cap * 2 : 32;
                CmpItem *grown = realloc(items, (size_t)cap * sizeof *items);
                if (!grown)
                    break;
                items = grown;
            }
            if (mailbox_item(p, len, n, &items[n]) != 0)
                break;
            n++;
        }
        p += len + 1;
    }
    completion_provide(job.token, items, n);
}

static void job_readable(int fd, void *ud) {
    (void)ud;
    for (;;) {
        char chunk[4096];
        ssize_t n = read(fd, chunk, sizeof chunk);
        if (n > 0) {
            strbuf_append(&job.out, chunk, (size_t)n);
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return;
        break; /* EOF (or a real error): hml is done */
    }
    job_stop(0);
    job_deliver();
    strbuf_free(&job.out);
}

/* The header `line` belongs to, following folded continuation lines up;
 * -1 when `line` is past the blank line that ends the headers. */
static int header_line(Buffer *buf, int line) {
    for (int i = 0; i <= line; i++)
        if (buf->rows[i].chars.len == 0)
            return -1;
    while (line > 0 && buf->rows[line].chars.len > 0 &&
           (buf->rows[line].chars.data[0] == ' ' ||
            buf->rows[line].chars.data[0] == '\t'))
        line--;
    return line;
}

static int is_addr_header(const Row *r) {
    static const char *const names[] = {"To:", "Cc:", "Bcc:"};
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
        size_t n = strlen(names[i]);
        if (r->chars.len >= n && strncasecmp(r->chars.data, names[i], n) == 0)
            return 1;
    }
    return 0;
}

static int addr_available(Buffer *buf) {
    return buf && buf->filetype && strcmp(buf->filetype, "mail-compose") == 0;
}

/* Re-ask hml when the typed mailbox grows past a word: '@' and '.' of
 * an address, the space between a first and last name. */
static int addr_is_trigger(Buffer *buf, int c) {
    (void)buf;
    return c == '@' || c == '.' || c == ' ' || c == '-';
}

static void addr_request(Buffer *buf, int line, int col, unsigned token) {
    if (line < 0 || line >= buf->num_rows ||
        (size_t)col > buf->rows[line].chars.len) {
        completion_provide(token, NULL, 0);
        return;
    }
    int hl = header_line(buf, line);
    if (hl < 0 || !is_addr_header(&buf->rows[hl])) {
        completion_provide(token, NULL, 0);
        return;
    }
    /* The mailbox being typed: after the last comma, or the header's
     * colon, on this line. */
    const char *s = buf->rows[line].chars.data;
    int start = col;
    while (start > 0 && s[start - 1] != ',' &&
           !(hl == line && s[start - 1] == ':'))
        start--;
    while (start < col && isspace((unsigned char)s[start]))
        start++;
    char *typed = strndup(s + start, (size_t)(col - start));
    if (!typed) {
        completion_provide(token, NULL, 0);
        return;
    }

    job_stop(SIGKILL);
    strbuf_free(&job.out);
    const char *argv[] = {"hml", "address", ADDR_LIMIT, "--", typed, NULL};
    int rc = proc_spawn(argv, PROC_STDERR_NULL, &job.pr);
    free(typed);
    if (rc != 0) {
        job.pr = (Proc){.pid = 0, .to_fd = -1, .from_fd = -1};
        completion_provide(token, NULL, 0);
        return;
    }
    job.token = token;
    job.start = start;
    job.out = strbuf_new();
    ed_loop_register("mail-address", job.pr.from_fd, job_readable, NULL);
}

static const CompletionSource addr_source = {
    .name = "mail-address",
    .available = addr_available,
    .is_trigger_char = addr_is_trigger,
    .request = addr_request,
};

void mail_complete_register(void) { completion_source_register(&addr_source); }
