/* hai plugin: hai's conversations in hed, over mail.
 *
 * hai keeps every session as a Maildir (hai/MAIL.md); this plugin
 * reads those directories straight off the disk and writes the
 * user's turns back with `hml send -t`, which delivers `@hai` mail
 * locally. No socket, no index: a session view is a directory
 * listing, "streaming" is the reply file hai writes while the model
 * talks, and a watcher timer re-renders when either changes.
 *
 *   :hai                 sessions of every agent, newest first
 *   :hai-tree            the agent tree (`hai tree`); <CR> jumps into
 *                        an agent's live conversation
 *   :hai-send [text]     mail the selection / paragraph / text into
 *                        the session being viewed, else the agent's
 *                        live conversation
 *   :hai-send-new [text] the same, starting a fresh session
 *   :hai-compose         a compose buffer (reply into the viewed
 *                        session, or a new one); C-c C-c sends
 *   :hai-verbose         whole tool results in the chat view
 *
 * In a chat: s says a line, S composes, r refreshes, v toggles
 * verbose, m opens the same thread in the mail plugin, t/l go to
 * the tree / the sessions, q closes. */

#include "hai.h"
#include "hai_session.h"
#include "hed.h"
#include "input/keybinds_builtins.h"
#include "select_loop.h"
#include "ui/ask.h"
#include "utils/buf_special.h"
#include "utils/term_cmd.h"
#include "utils/under_cursor.h"
#include "utils/yank.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

/* The mail plugin's thread view, when that plugin is in the build. */
extern void mail_open_thread(const char *tid) __attribute__((weak));

#define HAI_SESSIONS_BUF "hai://sessions"
#define HAI_TREE_BUF "hai://tree"
#define HAI_WATCH_MS 500

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

static char mailbox_dir[HAI_PATH] = "";
static char useraddr[160] = "user@hai";
static char agentaddr[160] = "main@hai";
static char sendcmd[512] = "hml send -t";
static int result_lines = 6;
static int verbose = 0;

void hai_set_mailbox(const char *dir) {
    if (dir && *dir)
        str_expand_tilde(dir, mailbox_dir, sizeof(mailbox_dir));
    else
        mailbox_dir[0] = '\0';
}

const char *hai_get_mailbox(void) {
    if (!mailbox_dir[0])
        fs_path_home_join(".mail/hai", mailbox_dir, sizeof(mailbox_dir));
    return mailbox_dir;
}

void hai_set_user(const char *addr) {
    if (addr && *addr)
        snprintf(useraddr, sizeof(useraddr), "%s", addr);
}

void hai_set_agent(const char *addr) {
    if (addr && *addr)
        snprintf(agentaddr, sizeof(agentaddr), "%s", addr);
}

void hai_set_send_cmd(const char *cmd) {
    if (cmd && *cmd)
        snprintf(sendcmd, sizeof(sendcmd), "%s", cmd);
}

void hai_set_result_lines(int n) { result_lines = n < 0 ? 0 : n; }

/* "pm@hai" → name "pm", domain "hai". */
static void addr_split(const char *addr, char *name, size_t ncap, char *domain,
                       size_t dcap) {
    const char *at = strchr(addr, '@');
    size_t n = at ? (size_t)(at - addr) : strlen(addr);
    if (n >= ncap)
        n = ncap - 1;
    memcpy(name, addr, n);
    name[n] = '\0';
    snprintf(domain, dcap, "%s", at ? at + 1 : "hai");
}

static const char *agent_domain(void) {
    static char domain[64];
    char name[128];
    addr_split(agentaddr, name, sizeof(name), domain, sizeof(domain));
    return domain;
}

static int is_me(const char *from) {
    return from && strstr(from, useraddr) != NULL;
}

/* ------------------------------------------------------------------ */
/* Chat buffers and their watcher                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    char bufname[HAI_PATH];
    HaiSession s;
    HaiSig sig;
    HaiMsg *msgs; /* stb_ds, file order */
    int loaded;
} Chat;

static Chat *chats = NULL; /* stb_ds; one per open chat buffer */
static int watching = 0;

static Chat *chat_find(const char *bufname) {
    for (ptrdiff_t i = 0; i < arrlen(chats); i++)
        if (strcmp(chats[i].bufname, bufname) == 0)
            return &chats[i];
    return NULL;
}

static void chat_drop(const char *bufname) {
    for (ptrdiff_t i = 0; i < arrlen(chats); i++) {
        if (strcmp(chats[i].bufname, bufname) == 0) {
            hai_msgs_free(chats[i].msgs);
            arrdel(chats, i);
            return;
        }
    }
}

static Chat *chat_current(void) {
    Buffer *buf = buf_cur();
    if (!buf || !buf->filetype || strcmp(buf->filetype, "hai-chat") != 0 ||
        !buf->filename)
        return NULL;
    return chat_find(buf->filename);
}

static void when_str(time_t t, char *out, size_t cap) {
    struct tm lt;
    if (t > 0 && localtime_r(&t, &lt))
        strftime(out, cap, "%d %b %H:%M", &lt);
    else
        out[0] = '\0';
}

/* Append `text` line by line, each prefixed by `prefix`, at most `max`
 * lines (0 = all) with a "… +N lines" tail. */
static void add_text(Buffer *buf, const char *prefix, const char *text,
                     int max) {
    int n = 0, total = 0;
    const char *end = text + strlen(text);
    while (end > text && (end[-1] == '\n' || end[-1] == ' '))
        end--; /* trailing blank lines say nothing */
    for (const char *p = text; p < end;) {
        const char *e = memchr(p, '\n', (size_t)(end - p));
        size_t len = e ? (size_t)(e - p) : (size_t)(end - p);
        total++;
        if (!max || n < max) {
            char *line = malloc(strlen(prefix) + len + 1);
            if (line) {
                size_t pl = strlen(prefix);
                memcpy(line, prefix, pl);
                memcpy(line + pl, p, len);
                buf_special_add(buf, line, pl + len);
                free(line);
            }
            n++;
        }
        if (!e)
            break;
        p = e + 1;
    }
    if (max && total > n)
        buf_special_addf(buf, "%s… +%d lines", prefix, total - n);
}

/* A block header: "● who — 18 Sep 17:19[ · note]". */
static void add_header(Buffer *buf, const char *mark, const char *who,
                       time_t when, const char *note) {
    char ts[64];
    when_str(when, ts, sizeof(ts));
    if (buf->num_rows > 0)
        buf_special_add(buf, "", 0);
    buf_special_addf(buf, "%s %s%s%s%s%s", mark, who, ts[0] ? " — " : "", ts,
                     note ? " · " : "", note ? note : "");
}

/* A turn that came in as mail is stored as the model saw it: a
 * From/Date/Subject/Message-ID envelope, a blank line, the body. The
 * header line already says who and when. */
static const char *strip_envelope(const char *body) {
    if (strncmp(body, "From: ", 6) != 0)
        return body;
    const char *p = body;
    while (*p && *p != '\n') {
        const char *e = strchr(p, '\n');
        const char *colon = strchr(p, ':');
        if (!colon || (e && colon > e))
            return body; /* not a header line: leave it whole */
        p = e ? e + 1 : p + strlen(p);
    }
    while (*p == '\n')
        p++;
    return p;
}

static const char *who_of(const Chat *c, const HaiMsg *m) {
    if (is_me(m->from))
        return "You";
    return m->from[0] ? m->from : c->s.agent;
}

/* Render the session into `buf`: the stored turns, then what waits
 * in the agent's inbox, then the reply being streamed. */
static void chat_render(Chat *c, Buffer *buf) {
    HaiSig sig;
    hai_session_sig(&c->s, hai_get_mailbox(), &sig);
    if (!c->loaded || sig.cur != c->sig.cur || sig.new != c->sig.new) {
        hai_msgs_free(c->msgs);
        c->msgs = NULL;
        hai_session_load(&c->s, &c->msgs);
        c->loaded = 1;
    }
    c->sig = sig;

    const char *subject = c->s.subject;
    for (ptrdiff_t i = 0; i < arrlen(c->msgs) && !subject[0]; i++)
        subject = c->msgs[i].subject;

    buf_special_clear(buf);
    buf_special_addf(buf, "Subject: %s", subject[0] ? subject : c->s.id);

    for (ptrdiff_t i = 0; i < arrlen(c->msgs); i++) {
        const HaiMsg *m = &c->msgs[i];
        if (strcmp(m->intent, "system") == 0)
            continue;
        if (strcmp(m->intent, "tool-result") == 0) {
            if (!verbose && result_lines == 0)
                continue;
            add_text(buf, "  │ ", m->body, verbose ? 0 : result_lines);
            continue;
        }
        if (strcmp(m->intent, "tool-call") == 0) {
            const char *calls = NULL;
            size_t n = hai_msg_content(m, &calls);
            add_header(buf, "●", who_of(c, m), m->when, NULL);
            if (n > 0) {
                char *content = malloc(n + 1);
                if (content) {
                    memcpy(content, m->body, n);
                    content[n] = '\0';
                    add_text(buf, "", content, 0);
                    free(content);
                }
            }
            for (const char *p = calls; p && *p;) {
                const char *e = strchr(p, '\n');
                size_t len = e ? (size_t)(e - p) : strlen(p);
                if (len > 3)
                    buf_special_addf(buf, "  → %.*s", (int)len - 3, p + 3);
                if (!e)
                    break;
                p = e + 1;
            }
            continue;
        }
        if (strcmp(m->intent, "ask") == 0) {
            add_header(buf, "?", who_of(c, m), m->when, "asks");
        } else if (strcmp(m->intent, "summary") == 0) {
            add_header(buf, "▪", "summary", m->when, NULL);
        } else if (strcmp(m->intent, "answer") == 0) {
            add_header(buf, "●", who_of(c, m), m->when, "answer");
        } else {
            add_header(buf, "●", who_of(c, m), m->when, NULL);
        }
        add_text(buf, "",
                 strcmp(m->role, "user") == 0 ? strip_envelope(m->body)
                                              : m->body,
                 0);
    }

    /* mailed, not yet taken by the agent */
    HaiMsg *pending = NULL;
    hai_session_pending(&c->s, hai_get_mailbox(), &pending);
    for (ptrdiff_t i = 0; i < arrlen(pending); i++) {
        add_header(buf, "●", who_of(c, &pending[i]), pending[i].when, "queued");
        add_text(buf, "", pending[i].body, 0);
    }
    hai_msgs_free(pending);

    /* the reply being streamed: the part after what is stored already
     * (an assistant message with tool calls lands in cur/ mid-run, and
     * the reply file holds every fragment of the run) */
    time_t started = 0;
    char *preview = hai_session_preview(&c->s, &started);
    if (preview) {
        const char *p = preview;
        for (ptrdiff_t i = 0; i < arrlen(c->msgs); i++) {
            const HaiMsg *m = &c->msgs[i];
            if (m->when + 1 < started || strcmp(m->role, "assistant") != 0 ||
                strcmp(m->intent, "tool-call") != 0)
                continue;
            size_t n = hai_msg_content(m, NULL);
            if (strncmp(p, m->body, n) == 0)
                p += n;
        }
        while (*p == '\n')
            p++;
        add_header(buf, "●", c->s.agent, 0, *p ? "writing…" : "working…");
        if (*p)
            add_text(buf, "", p, 0);
        free(preview);
    }

    buf->dirty = 0;
    free(buf->title);
    char title[1024];
    snprintf(title, sizeof(title), "%s: %s", c->s.agent,
             subject[0] ? subject : c->s.id);
    buf->title = strdup(title);
}

/* Re-render the chat behind buffer `idx`; a cursor sitting on the
 * last row follows it, any other keeps its place. */
static void chat_refresh(Chat *c, int idx) {
    Buffer *buf = &E.buffers[idx];
    int old_rows = buf->num_rows;
    int saved_y = buf->cursor ? buf->cursor->y : 0;
    chat_render(c, buf);
    int last = buf->num_rows > 0 ? buf->num_rows - 1 : 0;
    for (ptrdiff_t i = 0; i < arrlen(E.windows); i++) {
        Window *w = &E.windows[i];
        if (w->buffer_index != idx)
            continue;
        if (old_rows == 0 || w->cursor.y >= old_rows - 1)
            w->cursor.y = last;
        else if (w->cursor.y > last)
            w->cursor.y = last;
        int len = buf->num_rows ? (int)buf->rows[w->cursor.y].chars.len : 0;
        if (w->cursor.x > len)
            w->cursor.x = len;
    }
    if (buf->cursor) {
        buf->cursor->y = (old_rows == 0 || saved_y >= old_rows - 1) ? last
                         : saved_y > last                           ? last
                                                                    : saved_y;
        buf->cursor->x = 0;
    }
}

static void watch_tick(void *ud);

static void watch_arm(void) {
    ed_loop_timer_after("hai-watch", HAI_WATCH_MS, watch_tick, NULL);
    watching = 1;
}

static void watch_tick(void *ud) {
    (void)ud;
    int changed = 0;
    for (ptrdiff_t i = 0; i < arrlen(chats);) {
        int idx = buf_find_by_filename(chats[i].bufname);
        if (idx < 0) { /* closed behind our back */
            hai_msgs_free(chats[i].msgs);
            arrdel(chats, i);
            continue;
        }
        HaiSig sig;
        hai_session_sig(&chats[i].s, hai_get_mailbox(), &sig);
        if (!hai_sig_eq(&sig, &chats[i].sig)) {
            chat_refresh(&chats[i], idx);
            changed = 1;
        }
        i++;
    }
    if (changed)
        ed_render_frame();
    if (arrlen(chats) > 0)
        watch_arm();
    else
        watching = 0;
}

/* Open (or focus) the chat for session `s`. */
static void chat_open(const HaiSession *s) {
    char bufname[HAI_PATH];
    snprintf(bufname, sizeof(bufname), "hai://%s/%s", s->name, s->id);

    Chat *c = chat_find(bufname);
    if (!c) {
        Chat nc;
        memset(&nc, 0, sizeof(nc));
        snprintf(nc.bufname, sizeof(nc.bufname), "%s", bufname);
        nc.s = *s;
        arrput(chats, nc);
        c = &chats[arrlen(chats) - 1];
    }

    int idx = buf_find_by_filename(bufname);
    if (idx >= 0) {
        buf_switch(idx);
        chat_refresh(c, idx);
    } else {
        BufSpecial spec = {.name = bufname,
                           .filetype = "hai-chat",
                           .readonly = 1,
                           .as_filename = 1};
        idx = buf_special_get(&spec, NULL);
        if (idx < 0) {
            ed_set_status_message("hai: failed to open buffer");
            return;
        }
        chat_render(c, &E.buffers[idx]);
        buf_special_show(idx);
        Window *win = window_cur();
        if (win && E.buffers[idx].num_rows > 0) {
            win->cursor.y = E.buffers[idx].num_rows - 1;
            win->cursor.x = 0;
        }
    }
    if (!watching)
        watch_arm();
    ed_set_status_message("%s", E.buffers[idx].title);
}

static void chat_close_hook(HookBufferEvent *ev) {
    if (ev && ev->filename && strncmp(ev->filename, "hai://", 6) == 0)
        chat_drop(ev->filename);
}

/* ------------------------------------------------------------------ */
/* Sessions list                                                       */
/* ------------------------------------------------------------------ */

static HaiSession *sessions = NULL; /* stb_ds, one per row */

static void sessions_open(void) {
    arrfree(sessions);
    sessions = hai_sessions_scan(hai_get_mailbox(), agent_domain());

    BufSpecial spec = {.name = HAI_SESSIONS_BUF,
                       .title = "hai sessions",
                       .filetype = "hai-sessions",
                       .readonly = 1,
                       .as_filename = 1};
    int idx = buf_special_get(&spec, NULL);
    if (idx < 0) {
        ed_set_status_message("hai: failed to open buffer");
        return;
    }
    Buffer *buf = &E.buffers[idx];
    buf_special_clear(buf);
    if (arrlen(sessions) == 0)
        buf_special_addf(buf, "(no sessions under %s/s)", hai_get_mailbox());
    for (ptrdiff_t i = 0; i < arrlen(sessions); i++) {
        char ts[64];
        when_str(sessions[i].mtime, ts, sizeof(ts));
        buf_special_addf(buf, "%-12s %4d  %-12s  %s", ts, sessions[i].count,
                         sessions[i].agent,
                         sessions[i].subject[0] ? sessions[i].subject
                                                : sessions[i].id);
    }
    buf_special_show(idx);
    ed_set_status_message("hai: %ld sessions", (long)arrlen(sessions));
}

static void sessions_enter(void) {
    Window *win = window_cur();
    if (!win || win->cursor.y < 0 || win->cursor.y >= arrlen(sessions))
        return;
    HaiSession s = sessions[win->cursor.y];
    chat_open(&s);
}

/* ------------------------------------------------------------------ */
/* Agent tree                                                          */
/* ------------------------------------------------------------------ */

/* The agent named on a tree line: its local part, "" when none. */
static void tree_line_agent(const char *line, char *name, size_t cap) {
    name[0] = '\0';
    const char *at = strchr(line, '@');
    if (!at)
        return;
    const char *s = at;
    while (s > line && s[-1] != ' ')
        s--;
    size_t n = (size_t)(at - s);
    if (n >= cap)
        n = cap - 1;
    memcpy(name, s, n);
    name[n] = '\0';
}

static void tree_open(void) {
    char name[128], domain[64];
    addr_split(agentaddr, name, sizeof(name), domain, sizeof(domain));
    char cmd[300];
    snprintf(cmd, sizeof(cmd), "hai -s %s tree 2>&1", name);
    char **lines = NULL;
    int count = 0;
    int ok = term_cmd_capture(cmd, &lines, &count);

    BufSpecial spec = {.name = HAI_TREE_BUF,
                       .title = "hai agents",
                       .filetype = "hai-tree",
                       .readonly = 1,
                       .as_filename = 1};
    int idx = buf_special_get(&spec, NULL);
    if (idx < 0) {
        term_cmd_free(lines, count);
        ed_set_status_message("hai: failed to open buffer");
        return;
    }
    Buffer *buf = &E.buffers[idx];
    buf_special_clear(buf);
    if (!ok || count == 0)
        buf_special_addf(buf, "(%s is not reachable — is haid running?)",
                         agentaddr);
    else
        buf_special_add_lines(buf, lines, count);
    term_cmd_free(lines, count);
    buf_special_show(idx);
    ed_set_status_message("hai: agents — <CR> opens the live conversation, "
                          "T a terminal");
}

/* The live session of agent `name`, via `hai -s NAME status`. */
static int agent_live_session(const char *name, HaiSession *out) {
    char cmd[300];
    snprintf(cmd, sizeof(cmd), "hai -s %s status 2>/dev/null", name);
    char **lines = NULL;
    int count = 0;
    int found = 0;
    if (term_cmd_capture(cmd, &lines, &count)) {
        for (int i = 0; i < count && !found; i++) {
            if (lines[i] && strncmp(lines[i], "session ", 8) == 0 &&
                lines[i][8]) {
                hai_session_at(out, hai_get_mailbox(), agent_domain(), name,
                               lines[i] + 8);
                found = 1;
            }
        }
    }
    term_cmd_free(lines, count);
    return found;
}

static void tree_agent_under_cursor(char *name, size_t cap) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    name[0] = '\0';
    if (!buf || !win || win->cursor.y < 0 || win->cursor.y >= buf->num_rows)
        return;
    tree_line_agent(buf->rows[win->cursor.y].chars.data, name, cap);
}

static void tree_enter(void) {
    char name[128];
    tree_agent_under_cursor(name, sizeof(name));
    if (!name[0]) {
        ed_set_status_message("hai: no agent on this line");
        return;
    }
    HaiSession s;
    if (!agent_live_session(name, &s)) {
        ed_set_status_message("hai: %s@%s has no live session", name,
                              agent_domain());
        return;
    }
    chat_open(&s);
}

static void tree_terminal(void) {
    char name[128];
    tree_agent_under_cursor(name, sizeof(name));
    if (!name[0]) {
        ed_set_status_message("hai: no agent on this line");
        return;
    }
    char cmd[400];
    snprintf(cmd, sizeof(cmd),
             "${HAI_TERMINAL:-hterm} -e hai -s %s >/dev/null 2>&1 &", name);
    term_cmd_system(cmd);
    ed_set_status_message("hai: terminal on %s@%s", name, agent_domain());
}

/* ------------------------------------------------------------------ */
/* Sending                                                             */
/* ------------------------------------------------------------------ */

/* "Re: subject", unless it already is one. */
static void reply_subject(const char *subject, char *out, size_t cap) {
    if (strncasecmp(subject, "Re:", 3) == 0)
        snprintf(out, cap, "%s", subject);
    else
        snprintf(out, cap, "Re: %s", subject);
}

/* The first line of `text`, capped, as a subject for a new session. */
static void first_line_subject(const char *text, char *out, size_t cap) {
    while (*text == '\n' || *text == ' ')
        text++;
    size_t n = 0;
    while (text[n] && text[n] != '\n' && n < 60 && n + 1 < cap)
        n++;
    memcpy(out, text, n);
    out[n] = '\0';
    if (!n)
        snprintf(out, cap, "from hed");
}

/* Mail `text` into session `s` as the next turn: a reply to the last
 * message — or, when that is a question hai mailed the user, to the
 * mailed twin, which makes it the answer. */
static int send_into(const HaiSession *s, const char *text) {
    HaiMsg *msgs = NULL;
    hai_session_load(s, &msgs);
    const HaiMsg *last = arrlen(msgs) ? &msgs[arrlen(msgs) - 1] : NULL;

    char inreplyto[256] = "", refs[600] = "", subject[600];
    const char *subj = s->subject;
    for (ptrdiff_t i = 0; i < arrlen(msgs) && !subj[0]; i++)
        subj = msgs[i].subject;
    reply_subject(subj[0] ? subj : s->id, subject, sizeof(subject));

    if (last && strcmp(last->intent, "ask") == 0)
        hai_session_open_ask(s, hai_get_mailbox(), last->when - 1, inreplyto,
                             sizeof(inreplyto));
    if (!inreplyto[0] && last && last->mid[0])
        snprintf(inreplyto, sizeof(inreplyto), "%s", last->mid);
    if (inreplyto[0] && strcmp(inreplyto, s->root) != 0)
        snprintf(refs, sizeof(refs), "%s %s", s->root, inreplyto);
    else
        snprintf(refs, sizeof(refs), "%s", s->root);
    if (!inreplyto[0])
        snprintf(inreplyto, sizeof(inreplyto), "%s", s->root);
    hai_msgs_free(msgs);

    char mid[256], err[600];
    hai_new_mid(agent_domain(), mid, sizeof(mid));
    if (hai_mail(sendcmd, useraddr, s->agent, subject, mid, inreplyto, refs,
                 text, err, sizeof(err)) != 0) {
        ed_set_status_message("hai: %s", err);
        return -1;
    }
    ed_set_status_message("hai: sent to %s", s->agent);
    return 0;
}

/* Mail `text` to `agent` as a new session and open its chat: the
 * session's id is our Message-ID, so the chat exists (queued) before
 * the agent takes the message. `subject` NULL/empty = the first line. */
static int send_new(const char *agent, const char *subj, const char *text) {
    char name[128], domain[64], mid[256], id[256], subject[512], err[600];
    addr_split(agent, name, sizeof(name), domain, sizeof(domain));
    hai_new_mid(domain, mid, sizeof(mid));
    snprintf(id, sizeof(id), "%s", mid + 1);
    char *at = strchr(id, '@');
    if (at)
        *at = '\0';
    if (subj && *subj)
        snprintf(subject, sizeof(subject), "%s", subj);
    else
        first_line_subject(text, subject, sizeof(subject));
    if (hai_mail(sendcmd, useraddr, agent, subject, mid, NULL, NULL, text, err,
                 sizeof(err)) != 0) {
        ed_set_status_message("hai: %s", err);
        return -1;
    }
    HaiSession s;
    hai_session_at(&s, hai_get_mailbox(), domain, name, id);
    snprintf(s.subject, sizeof(s.subject), "%s", subject);
    chat_open(&s);
    ed_set_status_message("hai: new session with %s", agent);
    return 0;
}

/* The text a send command works on: the visual selection, else the
 * command's argument, else the paragraph under the cursor. malloc'd. */
static char *block_text(const char *args) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (buf && win && win->sel.type != SEL_NONE &&
        (E.mode == MODE_VISUAL || E.mode == MODE_VISUAL_LINE ||
         E.mode == MODE_VISUAL_BLOCK)) {
        TextSelection sel;
        if (kb_visual_to_textsel(buf, win, E.mode == MODE_VISUAL_BLOCK, &sel)) {
            YankData yd = yank_data_new(buf, &sel);
            StrBuf joined = strbuf_new();
            for (int i = 0; i < yd.num_rows; i++) {
                strbuf_append(&joined, yd.rows[i].data, yd.rows[i].len);
                strbuf_append_char(&joined, '\n');
            }
            yank_data_free(&yd);
            kb_visual_clear(win);
            ed_set_mode(MODE_NORMAL);
            char *out = strbuf_to_cstr(&joined);
            strbuf_free(&joined);
            return out;
        }
    }
    if (args && *args)
        return strdup(args);
    StrBuf para = strbuf_new();
    if (!buf_get_paragraph_under_cursor(&para) || para.len == 0) {
        strbuf_free(&para);
        return NULL;
    }
    char *out = strbuf_to_cstr(&para);
    strbuf_free(&para);
    return out;
}

static void cmd_hai_send(const char *args) {
    char *text = block_text(args);
    if (!text) {
        ed_set_status_message("hai-send: nothing to send");
        return;
    }
    Chat *c = chat_current();
    if (c) {
        send_into(&c->s, text);
    } else {
        char name[128], domain[64];
        addr_split(agentaddr, name, sizeof(name), domain, sizeof(domain));
        HaiSession s;
        if (agent_live_session(name, &s))
            send_into(&s, text);
        else
            send_new(agentaddr, NULL, text);
    }
    free(text);
}

static void cmd_hai_send_new(const char *args) {
    char *text = block_text(args);
    if (!text) {
        ed_set_status_message("hai-send-new: nothing to send");
        return;
    }
    send_new(agentaddr, NULL, text);
    free(text);
}

/* s in a chat: one line from the prompt. */
static void say_cb(const char *answer, void *ud) {
    char *bufname = ud;
    if (answer && *answer) {
        Chat *c = chat_find(bufname);
        if (c)
            send_into(&c->s, answer);
        else
            ed_set_status_message("hai: that chat is gone");
    }
    free(bufname);
}

static void cmd_hai_say(const char *args) {
    Chat *c = chat_current();
    if (!c) {
        ed_set_status_message("hai-say: not in a hai chat");
        return;
    }
    if (args && *args) {
        send_into(&c->s, args);
        return;
    }
    char label[200];
    snprintf(label, sizeof(label), "%s> ", c->s.agent);
    ask(label, NULL, say_cb, strdup(c->bufname));
}

/* ------------------------------------------------------------------ */
/* Compose buffers                                                     */
/* ------------------------------------------------------------------ */

static int compose_seq;

static void cmd_hai_compose(const char *args) {
    (void)args;
    Chat *c = chat_current();
    char bufname[64];
    snprintf(bufname, sizeof(bufname), "hai://compose-%d", ++compose_seq);
    BufSpecial spec = {.name = bufname,
                       .title = c ? "Reply to hai" : "Message to hai",
                       .filetype = "hai-compose",
                       .as_filename = 1};
    int idx = buf_special_get(&spec, NULL);
    if (idx < 0) {
        ed_set_status_message("hai: failed to open compose buffer");
        return;
    }
    Buffer *buf = &E.buffers[idx];
    buf_special_addf(buf, "From: %s", useraddr);
    buf_special_addf(buf, "To: %s", c ? c->s.agent : agentaddr);
    if (c) {
        char subject[600];
        const char *subj = c->s.subject;
        for (ptrdiff_t i = 0; i < arrlen(c->msgs) && !subj[0]; i++)
            subj = c->msgs[i].subject;
        reply_subject(subj[0] ? subj : c->s.id, subject, sizeof(subject));
        buf_special_addf(buf, "Subject: %s", subject);
        buf_special_addf(buf, "Hai-Session: %s", c->s.root);
    } else {
        buf_special_addf(buf, "Subject: ");
    }
    buf_special_add(buf, "", 0);
    buf_special_add(buf, "", 0);
    buf_special_show(idx);
    Window *win = window_cur();
    if (win) {
        win->cursor.y = c ? buf->num_rows - 1 : 2;
        win->cursor.x = c ? 0 : 9;
    }
    ed_set_mode(MODE_INSERT);
    ed_set_status_message("hai: C-c C-c sends");
}

static int row_header(Buffer *buf, const char *name, char *out, size_t cap) {
    size_t n = strlen(name);
    for (int i = 0; i < buf->num_rows; i++) {
        const char *r = buf->rows[i].chars.data;
        size_t len = buf->rows[i].chars.len;
        if (len == 0)
            break;
        if (len > n + 1 && strncasecmp(r, name, n) == 0 && r[n] == ':') {
            const char *v = r + n + 1;
            while (*v == ' ')
                v++;
            snprintf(out, cap, "%.*s", (int)(len - (size_t)(v - r)), v);
            return 1;
        }
    }
    out[0] = '\0';
    return 0;
}

static void cmd_hai_compose_send(const char *args) {
    (void)args;
    Buffer *buf = buf_cur();
    if (!buf || !buf->filetype || strcmp(buf->filetype, "hai-compose") != 0) {
        ed_set_status_message("hai: not a compose buffer");
        return;
    }
    char to[256], root[256], subject[512];
    row_header(buf, "To", to, sizeof(to));
    row_header(buf, "Hai-Session", root, sizeof(root));
    row_header(buf, "Subject", subject, sizeof(subject));
    if (!to[0]) {
        ed_set_status_message("hai: missing To:");
        return;
    }
    StrBuf body = strbuf_new();
    int in_body = 0;
    for (int i = 0; i < buf->num_rows; i++) {
        if (!in_body) {
            if (buf->rows[i].chars.len == 0)
                in_body = 1;
            continue;
        }
        strbuf_append(&body, buf->rows[i].chars.data, buf->rows[i].chars.len);
        strbuf_append_char(&body, '\n');
    }
    char *text = strbuf_to_cstr(&body);
    strbuf_free(&body);
    if (!text)
        return;

    int rc;
    char name[128], domain[64];
    addr_split(to, name, sizeof(name), domain, sizeof(domain));
    if (root[0]) {
        /* <id@domain> → the session it names */
        char id[256];
        snprintf(id, sizeof(id), "%s", root[0] == '<' ? root + 1 : root);
        char *at = strchr(id, '@');
        if (at)
            *at = '\0';
        HaiSession s;
        hai_session_at(&s, hai_get_mailbox(), domain, name, id);
        int bidx = buf_cur() - E.buffers;
        buf->dirty = 0;
        rc = send_into(&s, text);
        if (rc == 0) {
            buf_close(bidx);
            chat_open(&s);
        }
    } else {
        int bidx = buf_cur() - E.buffers;
        buf->dirty = 0;
        rc = send_new(to, subject, text);
        if (rc == 0) {
            /* send_new opened the chat; drop the compose behind it */
            int chat_idx = buf_cur() - E.buffers;
            buf_close(bidx);
            if (bidx < chat_idx)
                chat_idx--;
            buf_switch(chat_idx);
        }
    }
    free(text);
}

/* ------------------------------------------------------------------ */
/* Highlighting                                                        */
/* ------------------------------------------------------------------ */

#define HC_ME "\x1b[1;38;2;158;206;106m"    /* You: bold green   */
#define HC_AGENT "\x1b[1;38;2;122;162;247m" /* agent: bold blue  */
#define HC_ASK "\x1b[1;38;2;224;175;104m"   /* question: bold yellow */
#define HC_DIM COLOR_COMMENT
#define HC_TOOL COLOR_KEYWORD
#define HC_KEY COLOR_KEYWORD
#define HC_VAL COLOR_VARIABLE
#define HC_STATE_BUSY COLOR_CONSTANT
#define HC_STATE_ASK COLOR_LABEL

/* Byte offset of " — " in a header row, or len. */
static int dash_at(const char *raw, int len) {
    for (int i = 0; i + 4 < len; i++)
        if (raw[i] == ' ' && (unsigned char)raw[i + 1] == 0xE2 &&
            (unsigned char)raw[i + 2] == 0x80 &&
            (unsigned char)raw[i + 3] == 0x94 && raw[i + 4] == ' ')
            return i;
    return len;
}

static void chat_render_hook(const HookRenderEvent *e) {
    if (!e || !e->buf || !e->spans)
        return;
    Buffer *buf = e->buf;
    for (int row = e->row_start; row < e->row_end; row++) {
        if (row < 0 || row >= buf->num_rows)
            continue;
        const char *raw = buf->rows[row].chars.data;
        int len = (int)buf->rows[row].chars.len;
        if (!raw || len < 2)
            continue;
        if (row == 0 && strncmp(raw, "Subject:", 8) == 0) {
            attrspan_push(e->spans, row, 0, 8, HC_KEY, 0);
            attrspan_push(e->spans, row, 8, len, HC_VAL, 0);
            continue;
        }
        unsigned char b0 = (unsigned char)raw[0];
        /* ● / ▪ headers (UTF-8 e2 97 8f / e2 96 aa), ? asks */
        if ((b0 == 0xE2 && len > 4 && raw[3] == ' ') ||
            (raw[0] == '?' && raw[1] == ' ')) {
            int start = raw[0] == '?' ? 2 : 4;
            int d = dash_at(raw, len);
            const char *sgr =
                raw[0] == '?'                   ? HC_ASK
                : (unsigned char)raw[1] == 0x96 ? HC_DIM
                : d - start == 3 && strncmp(raw + start, "You", 3) == 0
                    ? HC_ME
                    : HC_AGENT;
            attrspan_push(e->spans, row, 0, d, sgr, 0);
            if (d < len)
                attrspan_push(e->spans, row, d, len, HC_DIM, 0);
            continue;
        }
        if (len > 6 && strncmp(raw, "  \xe2\x86\x92 ", 6) == 0) { /* → tool */
            int sp = 6;
            while (sp < len && raw[sp] != ' ')
                sp++;
            attrspan_push(e->spans, row, 0, sp, HC_TOOL, 0);
            if (sp < len)
                attrspan_push(e->spans, row, sp, len, HC_DIM, 0);
            continue;
        }
        if (len >= 5 && strncmp(raw, "  \xe2\x94\x82", 5) == 0) /* │ result */
            attrspan_push(e->spans, row, 0, len, HC_DIM, 0);
    }
}

static void sessions_render_hook(const HookRenderEvent *e) {
    if (!e || !e->buf || !e->spans)
        return;
    Buffer *buf = e->buf;
    for (int row = e->row_start; row < e->row_end; row++) {
        if (row < 0 || row >= buf->num_rows)
            continue;
        const char *raw = buf->rows[row].chars.data;
        int len = (int)buf->rows[row].chars.len;
        if (!raw || len < 20 || raw[0] == '(')
            continue;
        /* "%-12s %4d  %-12s  subject" */
        attrspan_push(e->spans, row, 0, 17, HC_DIM, 0);
        int a = 19, b = a;
        while (b < len && raw[b] != ' ')
            b++;
        attrspan_push(e->spans, row, a, b, HC_AGENT, 0);
    }
}

static void tree_render_hook(const HookRenderEvent *e) {
    if (!e || !e->buf || !e->spans)
        return;
    Buffer *buf = e->buf;
    for (int row = e->row_start; row < e->row_end; row++) {
        if (row < 0 || row >= buf->num_rows)
            continue;
        const char *raw = buf->rows[row].chars.data;
        int len = (int)buf->rows[row].chars.len;
        if (!raw || len < 2)
            continue;
        const char *at = memchr(raw, '@', (size_t)len);
        if (!at)
            continue;
        int s = (int)(at - raw);
        while (s > 0 && raw[s - 1] != ' ')
            s--;
        int end = (int)(at - raw);
        while (end < len && raw[end] != ' ')
            end++;
        attrspan_push(e->spans, row, s, end, HC_AGENT, 0);
        /* the state word after the address */
        int st = end;
        while (st < len && raw[st] == ' ')
            st++;
        int se = st;
        while (se < len && raw[se] != ' ')
            se++;
        if (se > st) {
            const char *sgr = strncmp(raw + st, "idle", 4) == 0 ? HC_DIM
                              : strncmp(raw + st, "asking", 6) == 0
                                  ? HC_STATE_ASK
                                  : HC_STATE_BUSY;
            attrspan_push(e->spans, row, st, se, sgr, 0);
        }
        int d = dash_at(raw, len);
        if (d < len)
            attrspan_push(e->spans, row, d, len, HC_DIM, 0);
    }
}

/* ------------------------------------------------------------------ */
/* Commands, keys, plugin                                              */
/* ------------------------------------------------------------------ */

static void cmd_hai(const char *args) {
    (void)args;
    sessions_open();
    ed_render_frame();
}

static void cmd_hai_tree(const char *args) {
    (void)args;
    tree_open();
    ed_render_frame();
}

static void cmd_hai_refresh(const char *args) {
    (void)args;
    Buffer *buf = buf_cur();
    if (!buf || !buf->filetype)
        return;
    if (strcmp(buf->filetype, "hai-sessions") == 0) {
        Window *win = window_cur();
        int y = win ? win->cursor.y : 0;
        sessions_open();
        if (win && y < buf->num_rows)
            win->cursor.y = y;
    } else if (strcmp(buf->filetype, "hai-tree") == 0) {
        tree_open();
    } else if (strcmp(buf->filetype, "hai-chat") == 0) {
        Chat *c = chat_current();
        if (c) {
            c->loaded = 0;
            chat_refresh(c, (int)(buf - E.buffers));
        }
    }
    ed_render_frame();
}

static void cmd_hai_verbose(const char *args) {
    int on = args_tristate(args, verbose);
    if (on < 0) {
        ed_set_status_message("usage: hai-verbose [on|off|toggle]");
        return;
    }
    verbose = on;
    for (ptrdiff_t i = 0; i < arrlen(chats); i++) {
        int idx = buf_find_by_filename(chats[i].bufname);
        if (idx >= 0)
            chat_refresh(&chats[i], idx);
    }
    ed_set_status_message("hai: tool results %s",
                          verbose ? "in full" : "abridged");
}

static void cmd_hai_open_entry(const char *args) {
    (void)args;
    sessions_enter();
}

static void cmd_hai_tree_enter(const char *args) {
    (void)args;
    tree_enter();
}

static void cmd_hai_tree_terminal(const char *args) {
    (void)args;
    tree_terminal();
}

/* m in a chat: the same conversation as a mail thread. */
static void cmd_hai_mail(const char *args) {
    (void)args;
    Chat *c = chat_current();
    if (!c) {
        ed_set_status_message("hai-mail: not in a hai chat");
        return;
    }
    if (!mail_open_thread) {
        ed_set_status_message("hai-mail: the mail plugin is not loaded");
        return;
    }
    char id[256];
    snprintf(id, sizeof(id), "%s", c->s.root + 1);
    size_t n = strlen(id);
    if (n && id[n - 1] == '>')
        id[n - 1] = '\0';
    char idq[600], cmd[800];
    shell_escape_single(id, idq, sizeof(idq));
    snprintf(cmd, sizeof(cmd),
             "hml new >/dev/null 2>&1; hml search --output=threads -- id:%s "
             "2>/dev/null",
             idq);
    char **lines = NULL;
    int count = 0;
    term_cmd_capture(cmd, &lines, &count);
    if (count > 0 && lines[0] && strncmp(lines[0], "thread:", 7) == 0)
        mail_open_thread(lines[0]);
    else
        ed_set_status_message("hai-mail: hml has no thread for %s", id);
    term_cmd_free(lines, count);
}

static int hai_init(void) {
    cmd("hai", cmd_hai, "hai: sessions of every agent");
    cmd("hai-tree", cmd_hai_tree, "hai: the agent tree");
    cmd("hai-send", cmd_hai_send,
        "hai: mail the selection / paragraph / [text] into the viewed "
        "session, else the agent's live conversation");
    cmd("hai-send-new", cmd_hai_send_new,
        "hai: mail the selection / paragraph / [text] as a new session");
    cmd("hai-compose", cmd_hai_compose,
        "hai: compose a message (a reply when viewing a session)");
    cmd("hai-verbose", cmd_hai_verbose,
        "hai: whole tool results in the chat view: on|off|toggle");
    cmd("hai-refresh", cmd_hai_refresh, "hai: reread the current hai buffer");

    cmd_ft("hai-sessions", "hai-open", cmd_hai_open_entry,
           "open the session under the cursor");
    cmd_ft("hai-tree", "hai-enter", cmd_hai_tree_enter,
           "open the live conversation of the agent under the cursor");
    cmd_ft("hai-tree", "hai-terminal", cmd_hai_tree_terminal,
           "a terminal on the agent under the cursor");
    cmd_ft("hai-chat", "hai-say", cmd_hai_say,
           "say a line into this session ([text], else a prompt)");
    cmd_ft("hai-chat", "hai-mail", cmd_hai_mail,
           "open this session as a mail thread");
    cmd_ft("hai-compose", "hai-compose-send", cmd_hai_compose_send,
           "send this message");

    cmapn_ft("hai-sessions", "<CR>", "hai-open", "open session");
    cmapn_ft("hai-sessions", "r", "hai-refresh", "refresh");
    cmapn_ft("hai-sessions", "t", "hai-tree", "agent tree");
    cmapn_ft("hai-sessions", "n", "hai-compose", "new message to hai");
    cmapn_ft("hai-sessions", "q", "bd", "close");

    cmapn_ft("hai-tree", "<CR>", "hai-enter", "open live conversation");
    cmapn_ft("hai-tree", "T", "hai-terminal", "terminal on this agent");
    cmapn_ft("hai-tree", "l", "hai", "sessions");
    cmapn_ft("hai-tree", "r", "hai-refresh", "refresh");
    cmapn_ft("hai-tree", "q", "bd", "close");

    cmapn_ft("hai-chat", "s", "hai-say", "say a line");
    cmapn_ft("hai-chat", "S", "hai-compose", "compose a reply");
    cmapn_ft("hai-chat", "r", "hai-refresh", "refresh");
    cmapn_ft("hai-chat", "v", "hai-verbose toggle", "whole tool results");
    cmapn_ft("hai-chat", "m", "hai-mail", "open as mail thread");
    cmapn_ft("hai-chat", "t", "hai-tree", "agent tree");
    cmapn_ft("hai-chat", "l", "hai", "sessions");
    cmapn_ft("hai-chat", "q", "bd", "close");

    cmapn_ft("hai-compose", "<C-c><C-c>", "hai-compose-send", "send");
    cmapi_ft("hai-compose", "<C-c><C-c>", "hai-compose-send", "send");
    cmapn_ft("hai-compose", "q", "bd", "close");

    cmapn(" aa", "hai", "hai sessions");
    cmapn(" at", "hai-tree", "hai agents");
    cmapn(" as", "hai-send", "send paragraph to hai");
    cmapv(" as", "hai-send", "send selection to hai");
    cmapn(" an", "hai-send-new", "new hai session from paragraph");
    cmapv(" an", "hai-send-new", "new hai session from selection");
    cmapn(" ac", "hai-compose", "compose to hai");

    hook_register_render(HOOK_RENDER_PRE, -1, "hai-chat", chat_render_hook);
    hook_register_render(HOOK_RENDER_PRE, -1, "hai-sessions",
                         sessions_render_hook);
    hook_register_render(HOOK_RENDER_PRE, -1, "hai-tree", tree_render_hook);
    hook_register_buffer(HOOK_BUFFER_CLOSE, -1, "*", chat_close_hook);
    return 0;
}

const Plugin plugin_hai = {
    .name = "hai",
    .desc = "hai's conversations over mail: sessions, agent tree, chat view "
            "with the streamed reply, send a block to an agent",
    .init = hai_init,
    .deinit = NULL,
};
