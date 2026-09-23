/* hai plugin: the agents, and text sent to them.
 *
 * Reading hai's conversations needs no view of its own — a session is
 * a Maildir and hml indexes it, so it is a mail thread like any other
 * and the mail plugin already reads threads (chat style by default),
 * filters, tags and replies to them. `:hai` points the mail list at
 * them, `:hai-dir` narrows it to the agents working in one directory.
 *
 * What mail cannot show is the agents themselves: who is alive, what
 * each is doing, on which model, in which directory. That is this
 * plugin's one buffer, `:hai-agents` (hai's own `hai tree`), and from
 * a row there you open that agent's conversation in mail, filter the
 * list to its sessions or its directory, say a line to it, or get a
 * terminal on it.
 *
 * Sending is mail too: a turn is `hml send -t` into the agent's inbox,
 * threaded so hai routes it into the session it answers (hai/MAIL.md).
 * `:hai-send` takes the visual selection or the paragraph under the
 * cursor, which is how a block of code or a note from any buffer
 * reaches an agent. */

#include "hai.h"
#include "hai_session.h"
#include "hed.h"
#include "input/keybinds_builtins.h"
#include "ui/ask.h"
#include "utils/buf_special.h"
#include "utils/term_cmd.h"
#include "utils/under_cursor.h"
#include "utils/yank.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* The mail plugin's, when it is in the build: the thread view, a
 * pre-filled compose, the sidebar's saved views. Weak — hai builds
 * and runs without it, minus the keys that open one. */
extern void mail_open_thread(const char *tid) __attribute__((weak));
extern void mail_compose_with_lines(const char *title, char **lines, int count)
    __attribute__((weak));
extern void mail_add_view(const char *name, const char *query)
    __attribute__((weak));

#define HAI_AGENTS_BUF "hai://agents"

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

static char mailbox_dir[HAI_PATH] = "";
static char useraddr[160] = "user@hai";
static char agentaddr[160] = "main@hai";
static char sendcmd[512] = "hml send -t";
static char base_query[512] = "path:hai/s/**";

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

void hai_set_query(const char *q) {
    if (q && *q)
        snprintf(base_query, sizeof(base_query), "%s", q);
}

/* "pm@hai" -> name "pm", domain "hai". */
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

/* hml indexes files; a session hai just wrote, or a turn just mailed,
 * is on disk but not yet in the index. `hml new` is mtime-gated, so
 * this costs milliseconds when nothing changed. */
static void index_refresh(void) {
    char **lines = NULL;
    int count = 0;
    term_cmd_capture("hml new >/dev/null 2>&1", &lines, &count);
    term_cmd_free(lines, count);
}

/* ------------------------------------------------------------------ */
/* The agents view                                                     */
/* ------------------------------------------------------------------ */

static void agents_open(void) {
    char name[128], domain[64];
    addr_split(agentaddr, name, sizeof(name), domain, sizeof(domain));
    char cmd[300];
    snprintf(cmd, sizeof(cmd), "hai -s %s tree 2>&1", name);
    char **lines = NULL;
    int count = 0;
    int ok = term_cmd_capture(cmd, &lines, &count);

    BufSpecial spec = {.name = HAI_AGENTS_BUF,
                       .title = "hai agents",
                       .filetype = "hai-agents",
                       .readonly = 1,
                       .as_filename = 1};
    int idx = buf_special_get(&spec, NULL);
    if (idx < 0) {
        term_cmd_free(lines, count);
        ed_set_status_message("hai: failed to open buffer");
        return;
    }
    Buffer *buf = &E.buffers[idx];
    int refresh = buf->num_rows > 0;
    buf_special_clear(buf);
    if (!ok || count == 0)
        buf_special_addf(buf, "(%s is not reachable — is haid running?)",
                         agentaddr);
    else
        buf_special_add_lines(buf, lines, count);
    term_cmd_free(lines, count);

    if (refresh) /* leave the cursor where it was */
        buf_switch(idx);
    else
        buf_special_show(idx);
    ed_set_status_message("hai: <CR> conversation · f sessions · d directory "
                          "· s say · c compose · T terminal");
}

/* The agent named on the row under the cursor ("" when none). `rest`
 * (optional) gets the remainder of the row, which carries its state,
 * directory and task. */
static int agent_row(char *name, size_t ncap, const char **rest) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    name[0] = '\0';
    if (rest)
        *rest = NULL;
    if (!buf || !win || win->cursor.y < 0 || win->cursor.y >= buf->num_rows)
        return 0;
    const char *line = buf->rows[win->cursor.y].chars.data;
    const char *at = strchr(line, '@');
    if (!at)
        return 0;
    const char *s = at;
    while (s > line && s[-1] != ' ')
        s--;
    size_t n = (size_t)(at - s);
    if (n >= ncap)
        n = ncap - 1;
    memcpy(name, s, n);
    name[n] = '\0';
    if (rest) {
        const char *e = strchr(at, ' ');
        *rest = e ? e : at + strlen(at);
    }
    return name[0] != '\0';
}

/* The directory an agent works in, as `hai tree` prints it:
 * "... (terminal) in ~/projects/hackable/hed on MODEL — TASK". Empty
 * when the row names none (that agent runs in the user's home). */
static void agent_dir(const char *rest, char *out, size_t cap) {
    out[0] = '\0';
    if (!rest)
        return;
    const char *p = strstr(rest, " in ");
    if (!p)
        return;
    p += 4;
    const char *e = strstr(p, " on ");
    const char *dash = strstr(p, " — ");
    if (!e || (dash && dash < e))
        e = dash;
    size_t n = e ? (size_t)(e - p) : strlen(p);
    char raw[HAI_PATH];
    if (n >= sizeof(raw))
        n = sizeof(raw) - 1;
    memcpy(raw, p, n);
    raw[n] = '\0';
    str_expand_tilde(raw, out, cap);
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

/* Open session `s` in the mail plugin: its root Message-ID is what
 * hml threads the whole conversation under. */
static void open_in_mail(const HaiSession *s) {
    if (!mail_open_thread) {
        ed_set_status_message("hai: the mail plugin is not loaded");
        return;
    }
    char id[256];
    snprintf(id, sizeof(id), "%s", s->root[0] == '<' ? s->root + 1 : s->root);
    size_t n = strlen(id);
    if (n && id[n - 1] == '>')
        id[n - 1] = '\0';

    index_refresh();
    char idq[600], cmd[800];
    shell_escape_single(id, idq, sizeof(idq));
    snprintf(cmd, sizeof(cmd),
             "hml search --output=threads -- id:%s 2>/dev/null", idq);
    char **lines = NULL;
    int count = 0;
    term_cmd_capture(cmd, &lines, &count);
    if (count > 0 && lines[0] && strncmp(lines[0], "thread:", 7) == 0)
        mail_open_thread(lines[0]);
    else
        ed_set_status_message("hai: no thread for %s yet", id);
    term_cmd_free(lines, count);
}

/* ------------------------------------------------------------------ */
/* The mail list, scoped                                               */
/* ------------------------------------------------------------------ */

/* Hand the mail plugin a query. Through the command registry rather
 * than its symbols: nothing links, and without that plugin it is
 * simply an unknown command. */
static int mail_query(const char *q) {
    char line[1200];
    snprintf(line, sizeof(line), "mail-query %s", q);
    index_refresh();
    if (command_execute_line(line))
        return 1;
    ed_set_status_message("hai: the mail plugin is not loaded");
    return 0;
}

static void mail_filter(const char *f) {
    char line[1200];
    snprintf(line, sizeof(line), "mail-filter %s", f);
    command_execute_line(line);
}

static void cmd_hai(const char *args) {
    (void)args;
    if (mail_query(base_query))
        ed_render_frame();
}

/* :hai-dir [path] — the sessions of the agents working in `path`. No
 * argument: the directory of the agent under the cursor in the agents
 * view, else the editor's own. The filter is a phrase search — every
 * session carries its working directory in the system message hai
 * stores with it. */
static void cmd_hai_dir(const char *args) {
    char dir[HAI_PATH] = "";
    const char *p = args ? args : "";
    while (*p == ' ')
        p++;
    if (*p) {
        str_expand_tilde(p, dir, sizeof(dir));
    } else {
        char name[128];
        const char *rest = NULL;
        Buffer *buf = buf_cur();
        if (buf && buf->filetype && strcmp(buf->filetype, "hai-agents") == 0 &&
            agent_row(name, sizeof(name), &rest))
            agent_dir(rest, dir, sizeof(dir));
        if (!dir[0])
            fs_getcwd(dir, sizeof(dir));
    }
    if (!dir[0]) {
        ed_set_status_message("hai-dir: no directory");
        return;
    }
    if (!mail_query(base_query))
        return;
    char filter[HAI_PATH + 64];
    snprintf(filter, sizeof(filter), "\"Working directory: %s\"", dir);
    mail_filter(filter);
    ed_set_status_message("hai: sessions working in %s", dir);
    ed_render_frame();
}

/* :hai-sessions [agent] — one agent's sessions in the mail list. */
static void cmd_hai_sessions(const char *args) {
    char name[128] = "", domain[64];
    const char *p = args ? args : "";
    while (*p == ' ')
        p++;
    if (*p)
        addr_split(p, name, sizeof(name), domain, sizeof(domain));
    else if (!agent_row(name, sizeof(name), NULL))
        addr_split(agentaddr, name, sizeof(name), domain, sizeof(domain));

    char q[1024];
    /* main's sessions sit directly under s/, a child's under its name */
    if (strcmp(name, "main") == 0)
        snprintf(q, sizeof(q), "%s and from:main@%s", base_query,
                 agent_domain());
    else
        snprintf(q, sizeof(q), "path:hai/s/%s/**", name);
    if (mail_query(q)) {
        ed_set_status_message("hai: %s@%s sessions", name, agent_domain());
        ed_render_frame();
    }
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

/* Mail `text` into session `s` as the next turn: a reply to its last
 * message — or, when that is a question hai mailed the user, to the
 * mailed twin, which is what makes it the answer. */
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

/* Mail `text` to `agent` as a new session: its id is the Message-ID
 * hed writes, so the thread exists the moment hai takes the message. */
static int send_new(const char *agent, const char *subj, const char *text) {
    char name[128], domain[64], mid[256], subject[512], err[600];
    addr_split(agent, name, sizeof(name), domain, sizeof(domain));
    hai_new_mid(domain, mid, sizeof(mid));
    if (subj && *subj)
        snprintf(subject, sizeof(subject), "%s", subj);
    else
        first_line_subject(text, subject, sizeof(subject));
    if (hai_mail(sendcmd, useraddr, agent, subject, mid, NULL, NULL, text, err,
                 sizeof(err)) != 0) {
        ed_set_status_message("hai: %s", err);
        return -1;
    }
    ed_set_status_message("hai: new session with %s", agent);
    return 0;
}

/* The agent a send is aimed at: the row under the cursor in the
 * agents view, else the configured one. */
static void target_agent(char *out, size_t cap) {
    char name[128];
    Buffer *buf = buf_cur();
    if (buf && buf->filetype && strcmp(buf->filetype, "hai-agents") == 0 &&
        agent_row(name, sizeof(name), NULL))
        snprintf(out, cap, "%s@%s", name, agent_domain());
    else
        snprintf(out, cap, "%s", agentaddr);
}

/* Into the agent's live conversation when it has one, else a new
 * session. `fresh` forces a new one. */
static void send_to_agent(const char *agent, const char *text, int fresh) {
    char name[128], domain[64];
    addr_split(agent, name, sizeof(name), domain, sizeof(domain));
    HaiSession s;
    if (!fresh && agent_live_session(name, &s))
        send_into(&s, text);
    else
        send_new(agent, NULL, text);
}

/* The text a send works on: the visual selection, else the command's
 * argument, else the paragraph under the cursor. malloc'd. */
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

static void send_block(const char *args, int fresh) {
    char *text = block_text(args);
    if (!text) {
        ed_set_status_message("hai-send: nothing to send");
        return;
    }
    char agent[160];
    target_agent(agent, sizeof(agent));
    send_to_agent(agent, text, fresh);
    free(text);
}

static void cmd_hai_send(const char *args) { send_block(args, 0); }
static void cmd_hai_send_new(const char *args) { send_block(args, 1); }

/* s in the agents view: one line from the prompt. */
static void say_cb(const char *answer, void *ud) {
    char *agent = ud;
    if (answer && *answer)
        send_to_agent(agent, answer, 0);
    free(agent);
}

static void cmd_hai_say(const char *args) {
    char agent[160];
    target_agent(agent, sizeof(agent));
    if (args && *args) {
        send_to_agent(agent, args, 0);
        return;
    }
    char label[200];
    snprintf(label, sizeof(label), "%s> ", agent);
    ask(label, NULL, say_cb, strdup(agent));
}

/* :hai-compose — the mail plugin's compose buffer, addressed to the
 * agent, sent by its own C-c C-c. */
static void cmd_hai_compose(const char *args) {
    (void)args;
    if (!mail_compose_with_lines) {
        ed_set_status_message("hai-compose: the mail plugin is not loaded");
        return;
    }
    char agent[160], from[300], to[300];
    target_agent(agent, sizeof(agent));
    snprintf(from, sizeof(from), "From: %s", useraddr);
    snprintf(to, sizeof(to), "To: %s", agent);
    char *lines[] = {from, to, (char *)"Subject: ", (char *)"", (char *)""};
    mail_compose_with_lines("Message to hai", lines, 5);
    ed_set_status_message("hai: compose to %s — C-c C-c sends", agent);
}

/* ------------------------------------------------------------------ */
/* Commands and keys                                                   */
/* ------------------------------------------------------------------ */

static void cmd_hai_agents(const char *args) {
    (void)args;
    agents_open();
    ed_render_frame();
}

static void cmd_hai_enter(const char *args) {
    (void)args;
    char name[128];
    if (!agent_row(name, sizeof(name), NULL)) {
        ed_set_status_message("hai: no agent on this line");
        return;
    }
    HaiSession s;
    if (!agent_live_session(name, &s)) {
        ed_set_status_message("hai: %s@%s has no live session", name,
                              agent_domain());
        return;
    }
    open_in_mail(&s);
}

static void cmd_hai_terminal(const char *args) {
    (void)args;
    char name[128];
    if (!agent_row(name, sizeof(name), NULL)) {
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
/* Highlighting                                                        */
/* ------------------------------------------------------------------ */

#define HC_AGENT "\x1b[1;38;2;122;162;247m" /* address: bold blue    */
#define HC_IDLE COLOR_COMMENT               /* idle: dim             */
#define HC_BUSY COLOR_CONSTANT              /* running: yellow       */
#define HC_ASKING COLOR_LABEL               /* asking: red           */
#define HC_DIR COLOR_STRING                 /* the working directory */
#define HC_DIM COLOR_COMMENT

static void agents_render_hook(const HookRenderEvent *e) {
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

        /* the address */
        int s = (int)(at - raw);
        while (s > 0 && raw[s - 1] != ' ')
            s--;
        int end = (int)(at - raw);
        while (end < len && raw[end] != ' ')
            end++;
        attrspan_push(e->spans, row, s, end, HC_AGENT, 0);

        /* the state word after it */
        int st = end;
        while (st < len && raw[st] == ' ')
            st++;
        int se = st;
        while (se < len && raw[se] != ' ')
            se++;
        if (se > st) {
            const char *sgr = strncmp(raw + st, "idle", 4) == 0     ? HC_IDLE
                              : strncmp(raw + st, "asking", 6) == 0 ? HC_ASKING
                                                                    : HC_BUSY;
            attrspan_push(e->spans, row, st, se, sgr, 0);
        }

        /* " in <dir>", up to " on MODEL" or the task dash */
        const char *in = strstr(raw + se, " in ");
        if (in) {
            int ds = (int)(in - raw) + 4;
            const char *on = strstr(raw + ds, " on ");
            const char *dash = strstr(raw + ds, " — ");
            const char *de = on;
            if (!de || (dash && dash < de))
                de = dash;
            attrspan_push(e->spans, row, ds, de ? (int)(de - raw) : len, HC_DIR,
                          0);
        }

        /* the task, after the em dash */
        for (int i = se; i + 4 < len; i++) {
            if (raw[i] == ' ' && (unsigned char)raw[i + 1] == 0xE2 &&
                (unsigned char)raw[i + 2] == 0x80 &&
                (unsigned char)raw[i + 3] == 0x94) {
                attrspan_push(e->spans, row, i, len, HC_DIM, 0);
                break;
            }
        }
    }
}

static int hai_init(void) {
    cmd("hai", cmd_hai, "hai: the sessions of every agent, in the mail list");
    cmd("hai-agents", cmd_hai_agents,
        "hai: the agents — state, model, directory, task");
    cmd("hai-sessions", cmd_hai_sessions,
        "hai: one agent's sessions in the mail list ([agent], else the one "
        "under the cursor)");
    cmd("hai-dir", cmd_hai_dir,
        "hai: the sessions of agents working in [path] (else this agent's "
        "directory, else the editor's)");
    cmd("hai-send", cmd_hai_send,
        "hai: mail the selection / paragraph / [text] to an agent, into its "
        "live conversation");
    cmd("hai-send-new", cmd_hai_send_new, "hai: the same, as a new session");
    cmd("hai-say", cmd_hai_say,
        "hai: say a line to an agent ([text], else a prompt)");
    cmd("hai-compose", cmd_hai_compose, "hai: compose a message to an agent");

    cmd_ft("hai-agents", "hai-open", cmd_hai_enter,
           "open this agent's conversation in mail");
    cmd_ft("hai-agents", "hai-terminal", cmd_hai_terminal,
           "a terminal on this agent");

    cmapn_ft("hai-agents", "<CR>", "hai-open", "open the conversation in mail");
    cmapn_ft("hai-agents", "f", "hai-sessions", "this agent's sessions");
    cmapn_ft("hai-agents", "d", "hai-dir",
             "sessions working in this directory");
    cmapn_ft("hai-agents", "s", "hai-say", "say a line to this agent");
    cmapn_ft("hai-agents", "c", "hai-compose", "compose to this agent");
    cmapn_ft("hai-agents", "T", "hai-terminal", "terminal on this agent");
    cmapn_ft("hai-agents", "r", "hai-agents", "refresh");
    cmapn_ft("hai-agents", "m", "hai", "every hai session in the mail list");
    cmapn_ft("hai-agents", "q", "bd", "close");

    cmapn(" aa", "hai-agents", "hai agents");
    cmapn(" am", "hai", "hai sessions in the mail list");
    cmapn(" ad", "hai-dir", "hai sessions for this directory");
    cmapn(" as", "hai-send", "send paragraph to hai");
    cmapv(" as", "hai-send", "send selection to hai");
    cmapn(" an", "hai-send-new", "new hai session from paragraph");
    cmapv(" an", "hai-send-new", "new hai session from selection");
    cmapn(" ac", "hai-compose", "compose to hai");

    hook_register_render(HOOK_RENDER_PRE, -1, "hai-agents", agents_render_hook);

    /* The mailbox sidebar's Views section (b in the mail list). */
    if (mail_add_view) {
        mail_add_view("hai sessions", base_query);
        mail_add_view("hai asks", "tag:hai:ask");
    }
    return 0;
}

const Plugin plugin_hai = {
    .name = "hai",
    .desc = "hai's agents: who is alive, where they work, their "
            "conversations in the mail list; mail a block to one",
    .init = hai_init,
    .deinit = NULL,
};
