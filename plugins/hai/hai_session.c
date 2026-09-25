/* The on-disk side of the hai plugin: hai's agent Maildirs read as
 * files (hai/MAIL.md is the format), and the user's turns mailed back.
 * Knows nothing about buffers or windows. */

#include "hai_session.h"
#include "fs/fs.h"
#include "lib/strutil.h"
#include "lib/vector.h"
#include "utils/term_cmd.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Files                                                               */
/* ------------------------------------------------------------------ */

static char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;
    size_t cap = 4096, n = 0;
    char *buf = malloc(cap);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    for (;;) {
        if (n + 1 >= cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) {
                free(buf);
                fclose(f);
                return NULL;
            }
            buf = nb;
        }
        size_t r = fread(buf + n, 1, cap - n - 1, f);
        if (r == 0)
            break;
        n += r;
    }
    fclose(f);
    buf[n] = '\0';
    if (len)
        *len = n;
    return buf;
}

/* Sorted names of the regular files in `dir` (stb_ds array of
 * malloc'd strings). Empty when the directory is missing. */
static int by_name(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static char **dir_files(const char *dir) {
    char **names = NULL;
    DIR *d = opendir(dir);
    if (!d)
        return NULL;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        arrput(names, strdup(e->d_name));
    }
    closedir(d);
    if (arrlen(names) > 1)
        qsort(names, (size_t)arrlen(names), sizeof(*names), by_name);
    return names;
}

static void free_names(char **names) {
    for (ptrdiff_t i = 0; i < arrlen(names); i++)
        free(names[i]);
    arrfree(names);
}

/* The epoch a Maildir file name starts with (hai and hml both name
 * files `<time>.<pid>...`). 0 when it doesn't. */
static time_t name_time(const char *name) {
    const char *b = fs_path_basename(name);
    if (!b || *b < '0' || *b > '9')
        return 0;
    return (time_t)strtol(b, NULL, 10);
}

/* ------------------------------------------------------------------ */
/* One message                                                         */
/* ------------------------------------------------------------------ */

/* Copy the value of header `name` out of the unfolded header block
 * `hdrs` (one "Name: value" per line). Returns 1 when present. */
static int header(const char *hdrs, const char *name, char *out, size_t cap) {
    size_t nlen = strlen(name);
    for (const char *p = hdrs; p && *p;
         p = strchr(p, '\n'), p = p ? p + 1 : NULL) {
        if (strncasecmp(p, name, nlen) != 0 || p[nlen] != ':')
            continue;
        const char *v = p + nlen + 1;
        while (*v == ' ' || *v == '\t')
            v++;
        const char *e = strchr(v, '\n');
        size_t n = e ? (size_t)(e - v) : strlen(v);
        while (n > 0 &&
               (v[n - 1] == ' ' || v[n - 1] == '\t' || v[n - 1] == '\r'))
            n--;
        if (n >= cap)
            n = cap - 1;
        memcpy(out, v, n);
        out[n] = '\0';
        return 1;
    }
    out[0] = '\0';
    return 0;
}

/* Split `text` into its header block (unfolded, malloc'd) and the
 * body (a pointer into `text`). */
static char *split_headers(char *text, char **body) {
    size_t cap = 256, n = 0;
    char *h = malloc(cap);
    if (!h)
        return NULL;
    char *p = text;
    while (*p && *p != '\n' && !(p[0] == '\r' && p[1] == '\n')) {
        char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        int cont = (*p == ' ' || *p == '\t');
        if (n + len + 2 >= cap) {
            cap = (n + len + 2) * 2;
            char *nh = realloc(h, cap);
            if (!nh) {
                free(h);
                return NULL;
            }
            h = nh;
        }
        if (cont && n > 0)
            n--; /* fold onto the previous line */
        memcpy(h + n, p, len);
        n += len;
        h[n++] = '\n';
        if (!e) {
            p += len;
            break;
        }
        p = e + 1;
    }
    h[n] = '\0';
    if (*p == '\r')
        p++;
    if (*p == '\n')
        p++;
    *body = p;
    return h;
}

/* The boundary of a multipart Content-Type, or 0. */
static int boundary_of(const char *ct, char *out, size_t cap) {
    const char *b = strstr(ct, "boundary=");
    if (!b)
        return 0;
    b += 9;
    int q = (*b == '"');
    if (q)
        b++;
    size_t n = 0;
    while (b[n] && b[n] != (q ? '"' : ';') && b[n] != ' ' && n + 1 < cap)
        n++;
    memcpy(out, b, n);
    out[n] = '\0';
    return n > 0;
}

/* The body of the first text/plain part of a multipart body, as a
 * malloc'd copy; NULL when there is none. */
static char *plain_part(char *body, const char *boundary) {
    char mark[300];
    snprintf(mark, sizeof(mark), "--%s", boundary);
    size_t mlen = strlen(mark);
    char *p = body;
    while ((p = strstr(p, mark))) {
        p += mlen;
        if (p[0] == '-' && p[1] == '-')
            return NULL; /* the closing boundary */
        if (*p == '\n')
            p++;
        char *pbody;
        char *ph = split_headers(p, &pbody);
        if (!ph)
            return NULL;
        char ct[256];
        header(ph, "Content-Type", ct, sizeof(ct));
        free(ph);
        if (strncasecmp(ct, "text/plain", 10) != 0) {
            p = pbody;
            continue;
        }
        char *end = strstr(pbody, mark);
        size_t n = end ? (size_t)(end - pbody) : strlen(pbody);
        /* the newline before the boundary belongs to the boundary */
        if (end && n > 0 && pbody[n - 1] == '\n')
            n--;
        char *out = malloc(n + 1);
        if (!out)
            return NULL;
        memcpy(out, pbody, n);
        out[n] = '\0';
        return out;
    }
    return NULL;
}

int hai_msg_read(const char *path, HaiMsg *m) {
    memset(m, 0, sizeof(*m));
    size_t len = 0;
    char *text = read_file(path, &len);
    if (!text)
        return -1;
    snprintf(m->file, sizeof(m->file), "%s", path);
    m->when = name_time(path);

    char *body = NULL;
    char *h = split_headers(text, &body);
    if (!h) {
        free(text);
        return -1;
    }
    header(h, "From", m->from, sizeof(m->from));
    header(h, "Subject", m->subject, sizeof(m->subject));
    header(h, "Message-ID", m->mid, sizeof(m->mid));
    header(h, "In-Reply-To", m->inreplyto, sizeof(m->inreplyto));
    header(h, "References", m->refs, sizeof(m->refs));
    header(h, "Hai-Role", m->role, sizeof(m->role));
    header(h, "Hai-Intent", m->intent, sizeof(m->intent));
    header(h, "Hai-Tool", m->tool, sizeof(m->tool));
    header(h, "Hai-Conversation", m->conv, sizeof(m->conv));
    char date[128];
    m->date =
        header(h, "Date", date, sizeof(date)) ? str_parse_rfc2822(date) : -1;
    if (m->date <= 0)
        m->date = m->when;
    if (!m->role[0]) /* mail from outside: what hai makes of it */
        snprintf(m->role, sizeof(m->role), "user");
    if (!m->intent[0])
        snprintf(m->intent, sizeof(m->intent), "message");

    char ct[512], boundary[256];
    header(h, "Content-Type", ct, sizeof(ct));
    free(h);
    if (strncasecmp(ct, "multipart/", 10) == 0 &&
        boundary_of(ct, boundary, sizeof(boundary)))
        m->body = plain_part(body, boundary);
    if (!m->body)
        m->body = strdup(body ? body : "");
    free(text);
    return m->body ? 0 : -1;
}

void hai_msgs_free(HaiMsg *msgs) {
    for (ptrdiff_t i = 0; i < arrlen(msgs); i++)
        free(msgs[i].body);
    arrfree(msgs);
}

/* ------------------------------------------------------------------ */
/* Sessions                                                            */
/* ------------------------------------------------------------------ */

void hai_session_at(HaiSession *s, const char *mailbox, const char *domain,
                    const char *name, const char *id) {
    memset(s, 0, sizeof(*s));
    snprintf(s->id, sizeof(s->id), "%s", id);
    snprintf(s->name, sizeof(s->name), "%s", name);
    snprintf(s->agent, sizeof(s->agent), "%s@%s", name, domain);
    snprintf(s->root, sizeof(s->root), "<%s@%s>", id, domain);
    snprintf(s->box, sizeof(s->box), "%s/%s", mailbox, name);
}

/* The session id of a root, as hai makes it: the local part, with
 * '/', ' ' and a leading '.' turned into '_'. */
static void id_of(const char *mid, char *out, size_t cap) {
    size_t i = 0;
    const char *p = mid;
    if (*p == '<')
        p++;
    for (; *p && *p != '@' && *p != '>' && i + 1 < cap; p++, i++)
        out[i] = (*p == '/' || *p == ' ' || (*p == '.' && !i)) ? '_' : *p;
    out[i] = '\0';
}

/* A Maildir file flagged T: deleted, no part of any session. */
static int trashed(const char *name) {
    const char *info = strstr(name, ":2,");
    return info && strchr(info + 3, 'T');
}

/* The Hai-Conversation of a file hai stored (Hai-Role present), from
 * its first 8 KB; 0 when it is not one. */
static int conv_of(const char *path, char *out, size_t cap) {
    char buf[8193], role[16];
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    char *body = NULL;
    char *h = split_headers(buf, &body);
    if (!h)
        return 0;
    int ok = header(h, "Hai-Role", role, sizeof(role)) &&
             header(h, "Hai-Conversation", out, cap);
    free(h);
    return ok;
}

int hai_session_of_file(const char *path, const char *mailbox,
                        const char *domain, HaiSession *out) {
    char name[128], conv[256], id[128];
    size_t mlen = strlen(mailbox);
    if (strncmp(path, mailbox, mlen) != 0 || path[mlen] != '/')
        return 0;
    const char *rel = path + mlen + 1, *sep = strchr(rel, '/');
    if (!sep || strncmp(sep, "/cur/", 5) != 0 || strchr(sep + 5, '/') ||
        (size_t)(sep - rel) >= sizeof(name) || trashed(sep + 5))
        return 0;
    memcpy(name, rel, (size_t)(sep - rel));
    name[sep - rel] = '\0';
    if (!conv_of(path, conv, sizeof(conv)))
        return 0;
    id_of(conv, id, sizeof(id));
    hai_session_at(out, mailbox, domain, name, id);
    snprintf(out->root, sizeof(out->root), "%.159s", conv);
    return 1;
}

/* The session's files in name order — which is time order, and the
 * order hai loads them (stb_ds array of malloc'd paths; free_names
 * it). `*exists` (optional) says whether the agent's cur/ is there. */
static char **session_files(const HaiSession *s, int *exists) {
    char path[HAI_PATH + 8], conv[256], id[128];
    char **all = NULL;
    snprintf(path, sizeof(path), "%s/cur", s->box);
    if (exists)
        *exists = fs_is_dir(path);
    char **names = dir_files(path);
    for (ptrdiff_t i = 0; i < arrlen(names); i++) {
        if (trashed(names[i]))
            continue;
        size_t n = strlen(path) + strlen(names[i]) + 2;
        char *full = malloc(n);
        if (!full)
            continue;
        snprintf(full, n, "%s/%s", path, names[i]);
        if (conv_of(full, conv, sizeof(conv))) {
            id_of(conv, id, sizeof(id));
            if (strcmp(id, s->id) == 0) {
                arrput(all, full);
                continue;
            }
        }
        free(full);
    }
    free_names(names);
    return all;
}

/* Modification time in milliseconds — a directory written twice in
 * one second still reads as changed. 0 when there is no such path. */
long hai_mtime(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0)
        return 0;
    return (long)st.st_mtim.tv_sec * 1000 + st.st_mtim.tv_nsec / 1000000;
}

/* The reply a run of the session is streaming into
 * <box>/tmp/reply.<id>, or NULL when none is on. malloc'd; `*started`
 * gets the moment the file appeared. */
char *hai_session_preview(const HaiSession *s, time_t *started) {
    char path[HAI_PATH + 160];
    snprintf(path, sizeof(path), "%s/tmp/reply.%s", s->box, s->id);
    struct stat st;
    if (stat(path, &st) != 0)
        return NULL;
    if (started)
        *started = st.st_ctime;
    char *text = read_file(path, NULL);
    return text ? text : strdup("");
}

/* Content of an assistant message as the wire carried it: the text
 * part minus the "-> name args" lines hai appends per call, and the
 * newline between. Returns its length. */
size_t hai_msg_content(const HaiMsg *m) {
    const char *b = m->body ? m->body : "";
    size_t n = strlen(b);
    if (strcmp(m->intent, "tool-call") != 0)
        return n;
    for (;;) { /* drop the trailing call lines */
        size_t e = n;
        while (e > 0 && b[e - 1] == '\n')
            e--;
        size_t st = e;
        while (st > 0 && b[st - 1] != '\n')
            st--;
        if (e == st || strncmp(b + st, "-> ", 3) != 0)
            break;
        n = st;
    }
    while (n > 0 && b[n - 1] == '\n')
        n--;
    return n;
}

int hai_session_load(HaiSession *s, HaiMsg **out) {
    int exists = 0;
    char **all = session_files(s, &exists);
    if (!exists) {
        free_names(all);
        return -1;
    }
    for (ptrdiff_t i = 0; i < arrlen(all); i++) {
        HaiMsg m;
        if (hai_msg_read(all[i], &m) != 0)
            continue;
        snprintf(s->root, sizeof(s->root), "%.159s", m.conv);
        arrput(*out, m);
    }
    free_names(all);
    return 0;
}

void hai_session_open_ask(const HaiSession *s, const char *mailbox,
                          time_t since, char *mid, size_t cap) {
    mid[0] = '\0';
    time_t best = 0;
    const char *subs[] = {"new", "cur"};
    for (int k = 0; k < 2; k++) {
        char path[HAI_PATH + 16];
        snprintf(path, sizeof(path), "%s/user/%s", mailbox, subs[k]);
        char **names = dir_files(path);
        for (ptrdiff_t i = 0; i < arrlen(names); i++) {
            time_t t = name_time(names[i]);
            if (t < since || t < best)
                continue;
            char full[HAI_PATH + 300];
            snprintf(full, sizeof(full), "%s/%s", path, names[i]);
            HaiMsg m;
            if (hai_msg_read(full, &m) != 0)
                continue;
            if (strcmp(m.intent, "ask") == 0 && strstr(m.refs, s->root) &&
                m.mid[0]) {
                snprintf(mid, cap, "%s", m.mid);
                best = t;
            }
            free(m.body);
        }
        free_names(names);
    }
}

/* ------------------------------------------------------------------ */
/* Writing                                                             */
/* ------------------------------------------------------------------ */

void hai_new_mid(const char *domain, char *out, size_t cap) {
    static int seq;
    snprintf(out, cap, "<%ld.%d.%d@%s>", (long)time(NULL), (int)getpid(), ++seq,
             domain);
}

int hai_mail(const char *sendcmd, const char *from, const char *to,
             const char *subject, const char *mid, const char *inreplyto,
             const char *references, const char *body, char *err,
             size_t errcap) {
    char date[64];
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S %z", &lt);

    size_t blen = strlen(body);
    size_t cap = blen + 4096;
    char *msg = malloc(cap);
    if (!msg) {
        snprintf(err, errcap, "out of memory");
        return -1;
    }
    int n = snprintf(msg, cap,
                     "From: %s\nTo: %s\nSubject: %s\nDate: %s\nMessage-ID: "
                     "%s\n%s%s%s%s%s%s"
                     "MIME-Version: 1.0\nContent-Type: text/plain; "
                     "charset=utf-8\nContent-Transfer-Encoding: 8bit\n\n",
                     from, to, subject, date, mid,
                     inreplyto && *inreplyto ? "In-Reply-To: " : "",
                     inreplyto && *inreplyto ? inreplyto : "",
                     inreplyto && *inreplyto ? "\n" : "",
                     references && *references ? "References: " : "",
                     references && *references ? references : "",
                     references && *references ? "\n" : "");
    memcpy(msg + n, body, blen);
    size_t len = (size_t)n + blen;
    if (len == 0 || msg[len - 1] != '\n')
        msg[len++] = '\n';

    char *out = NULL;
    int rc = term_cmd_filter(sendcmd, msg, len, &out, NULL);
    free(msg);
    if (rc != 0) {
        char line[200] = "";
        if (out) {
            snprintf(line, sizeof(line), "%s", out);
            str_chomp(line);
        }
        snprintf(err, errcap, "%s exited %d%s%s", sendcmd, rc,
                 line[0] ? ": " : "", line);
    }
    free(out);
    return rc == 0 ? 0 : -1;
}
