#ifndef HAI_SESSION_H
#define HAI_SESSION_H

#include <stddef.h>
#include <time.h>

/* hai keeps every conversation as a Maildir under `<mailbox>/s/`
 * (hai/MAIL.md): one file per turn, no encoding, `Hai-*` headers say
 * what each file is. This reads those files straight off the disk —
 * no index, no daemon — and writes the user's turns back through a
 * sendmail-shaped command (`hml send -t` delivers `@hai` locally). */

#define HAI_PATH 1024

typedef struct {
    char file[HAI_PATH];
    char from[256];
    char subject[512];
    char mid[256];       /* Message-ID, brackets kept */
    char inreplyto[256]; /* In-Reply-To, brackets kept */
    char refs[1024];     /* References */
    char role[16];       /* Hai-Role: system user assistant tool */
    char intent[16];     /* Hai-Intent: message system tool-call
                            tool-result ask answer summary */
    char tool[64];       /* Hai-Tool on tool results */
    time_t when;         /* the file's own time (its name) */
    time_t date;         /* its Date header, `when` when it has none */
    char *body;          /* the text part, malloc'd, NUL-terminated */
} HaiMsg;

typedef struct {
    char dir[HAI_PATH];     /* <mailbox>/s/<id> or <mailbox>/s/<name>/<id> */
    char id[128];           /* the session id: the root's local part */
    char name[128];         /* the agent's local part: main, pm, pm.scout */
    char agent[160];        /* its address: name@hai */
    char root[160];         /* <id@hai> */
    char subject[512];      /* the root message's Subject */
    char workdir[HAI_PATH]; /* where the agent works; "" = not known */
    long mtime;             /* last activity, in milliseconds */
    int count;              /* files in cur + new */
} HaiSession;

/* Fill `s` for the session `id` of agent `name` under `mailbox`.
 * Nothing is read; the directory need not exist yet. */
void hai_session_at(HaiSession *s, const char *mailbox, const char *domain,
                    const char *name, const char *id);

/* Where the agent works, from the system message the session stores
 * ("Working directory: <path>."; the user's home when the prompt says
 * nothing, since that is where an agent without one runs). "" when the
 * session holds no system message. Filled in by hai_sessions_scan;
 * call it for a session built with hai_session_at. */
void hai_session_workdir(const HaiSession *s, char *out, size_t cap);

/* The messages of a session in file-name order — the order hai loads
 * them (stb_ds array; hai_msgs_free it). Returns -1 when the
 * directory cannot be read. */
int hai_session_load(const HaiSession *s, HaiMsg **out);
void hai_msgs_free(HaiMsg *msgs);

/* Parse one message file. Returns 0 on success. */
int hai_msg_read(const char *path, HaiMsg *m);

/* The Message-ID of the question hai mailed the user for this session
 * (<mailbox>/user/{new,cur}: Hai-Intent: ask, References the root),
 * the newest one written at `since` or later — the twin of the `ask`
 * note the session holds, which is what an answer must reply to.
 * "" when none. */
void hai_session_open_ask(const HaiSession *s, const char *mailbox,
                          time_t since, char *mid, size_t cap);

/* Mail a message through `sendcmd` (reads RFC 822 on stdin). `mid` is
 * the Message-ID to write (with brackets) — the session id when this
 * starts a session. `inreplyto`/`references` may be NULL. Returns 0
 * on success; `err` gets the command's status otherwise. */
int hai_mail(const char *sendcmd, const char *from, const char *to,
             const char *subject, const char *mid, const char *inreplyto,
             const char *references, const char *body, char *err,
             size_t errcap);

/* A fresh Message-ID for a message hed writes: <time.pid.seq@domain>. */
void hai_new_mid(const char *domain, char *out, size_t cap);

#endif
