#include "editor.h"
#include "commands.h"
#include "registers.h"
#include "lib/safe_string.h"
#include "lib/log.h"
#include "lib/strutil.h"
#include "terminal.h"
#include "command_mode.h"
#include "commands/cmd_search.h"
#include "prompt.h"
#include "stb_ds.h"

#include <ctype.h>
#include <dirent.h>
#include "lib/path_limits.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ===========================================================================
 * The ":" prompt
 *
 * Vtable layout: on_key delegates to prompt_default_on_key, which calls
 *   complete()  on Tab,
 *   history()   on Up/Down arrow,
 *   on_submit() on Enter, on_cancel() on Esc.
 *
 * Storage lives in the Prompt itself (p->buf/p->len). State specific to
 * the colon prompt — file-path completion candidates and a "command-name
 * pending fzf escalation" flag — lives in CmdPromptState attached via
 * p->state.
 * =========================================================================== */

typedef struct {
    char **items;
    int    count;
    int    index;
    char   base[256];
    char   prefix[128];
    int    active;
    /* 0 = none/path mode; 1 = command-name with multiple candidates and
     * a pending fzf escalation on the next Tab. */
    int    cmdname_pending;
} CmdComp;

typedef struct {
    CmdComp comp;
} CmdPromptState;

static CmdPromptState g_cmd_state;

/* History hook registry — plugins (e.g. tmux) plug in here. */
typedef struct { CmdPromptHistoryHook fn; void *ud; } HistoryHook;
static HistoryHook *g_history_hooks = NULL;

void cmd_prompt_history_register(CmdPromptHistoryHook fn, void *ud) {
    HistoryHook h = { fn, ud };
    arrput(g_history_hooks, h);
}

/* ----- completion: file-path and command-name ---------------------------- */

static void cmdcomp_clear(CmdComp *c) {
    if (c->items) {
        for (int i = 0; i < c->count; i++) free(c->items[i]);
        free(c->items);
    }
    c->items           = NULL;
    c->count           = 0;
    c->index           = 0;
    c->base[0]         = '\0';
    c->prefix[0]       = '\0';
    c->active          = 0;
    c->cmdname_pending = 0;
}

static int cmdcomp_is_cmdname_position(Prompt *p) {
    for (int i = 0; i < p->len; i++)
        if (p->buf[i] == ' ') return 0;
    return 1;
}

/* Replace the leading token in p->buf with `replacement`. Optionally append
 * a trailing space. Refuses to truncate if the result would overflow. */
static void cmdcomp_replace_first_token(Prompt *p, const char *replacement,
                                         int add_space) {
    int tok_end = p->len;
    for (int i = 0; i < p->len; i++) {
        if (p->buf[i] == ' ') { tok_end = i; break; }
    }
    int rlen    = (int)strlen(replacement);
    int extra   = add_space ? 1 : 0;
    int tail    = p->len - tok_end;
    int new_len = rlen + extra + tail;
    if (new_len >= (int)sizeof(p->buf)) return;
    memmove(p->buf + rlen + extra, p->buf + tok_end, (size_t)tail);
    memcpy(p->buf, replacement, (size_t)rlen);
    if (add_space) p->buf[rlen] = ' ';
    p->len = new_len;
    p->buf[p->len] = '\0';
    p->cursor = p->len;
}

static int cmdcomp_complete_cmdname(Prompt *p, CmdComp *c) {
    char prefix[128];
    int plen = p->len;
    if (plen >= (int)sizeof(prefix)) plen = (int)sizeof(prefix) - 1;
    memcpy(prefix, p->buf, (size_t)plen);
    prefix[plen] = '\0';

    int matches[256];
    int n = 0;
    for (ptrdiff_t i = 0; i < arrlen(commands)
                       && n < (ptrdiff_t)(sizeof(matches)/sizeof(matches[0])); i++) {
        if (commands[i].name &&
            strncmp(commands[i].name, prefix, (size_t)plen) == 0)
            matches[n++] = (int)i;
    }
    if (n == 0) {
        ed_set_status_message("no matching commands");
        c->cmdname_pending = 0;
        return 0;
    }
    if (n == 1) {
        cmdcomp_replace_first_token(p, commands[matches[0]].name, 1);
        c->cmdname_pending = 0;
        return 1;
    }
    /* >1 match: extend to longest common prefix. */
    const char *first = commands[matches[0]].name;
    int lcp_len = (int)strlen(first);
    for (int i = 1; i < n; i++) {
        const char *nm = commands[matches[i]].name;
        int j = 0;
        while (j < lcp_len && nm[j] && first[j] == nm[j]) j++;
        lcp_len = j;
    }
    int changed = 0;
    if (lcp_len > plen) {
        char lcp[128];
        if (lcp_len >= (int)sizeof(lcp)) lcp_len = (int)sizeof(lcp) - 1;
        memcpy(lcp, first, (size_t)lcp_len);
        lcp[lcp_len] = '\0';
        cmdcomp_replace_first_token(p, lcp, 0);
        changed = 1;
    }
    c->cmdname_pending = 1;
    ed_set_status_message("%d matches — Tab again for fzf", n);
    return changed;
}

static void cmdcomp_apply_token(Prompt *p, const char *replacement) {
    int len = p->len;
    int start = 0;
    for (int i = len - 1; i >= 0; i--) {
        if (p->buf[i] == ' ') { start = i + 1; break; }
    }
    int rlen = (int)strlen(replacement);
    if (start + rlen >= (int)sizeof(p->buf))
        rlen = (int)sizeof(p->buf) - 1 - start;
    memcpy(p->buf + start, replacement, (size_t)rlen);
    p->len         = start + rlen;
    p->buf[p->len] = '\0';
    p->cursor      = p->len;
}

/* snprintf truncation here is intentional: paths longer than PATH_MAX or
 * the destination buffer are silently dropped, matching how the rest of
 * the path-completion code treats oversize entries. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
static void cmdcomp_build_filepath(Prompt *p, CmdComp *c) {
    cmdcomp_clear(c);
    const char *home = getenv("HOME");
    int len   = p->len;
    int start = 0;
    for (int i = len - 1; i >= 0; i--) {
        if (p->buf[i] == ' ') { start = i + 1; break; }
    }
    char token[PATH_MAX];
    int  tlen = len - start;
    if (tlen < 0) tlen = 0;
    if (tlen > (int)sizeof(token) - 1) tlen = (int)sizeof(token) - 1;
    memcpy(token, p->buf + start, (size_t)tlen);
    token[tlen] = '\0';
    if (tlen == 0) return;
    char first = token[0];
    if (!(first == '.' || first == '~' || first == '/'))
        return;

    char full[PATH_MAX];
    if (token[0] == '~' && home) {
        if (token[1] == '/' || token[1] == '\0')
            snprintf(full, sizeof(full), "%s/%s", home,
                     token[1] ? token + 2 - 1 : "");
        else
            snprintf(full, sizeof(full), "%s", token);
    } else {
        snprintf(full, sizeof(full), "%s", token);
    }
    const char *slash = strrchr(full, '/');
    char base[PATH_MAX];
    char pref[PATH_MAX];
    if (slash) {
        size_t blen = (size_t)(slash - full + 1);
        if (blen >= sizeof(base)) blen = sizeof(base) - 1;
        memcpy(base, full, blen); base[blen] = '\0';
        snprintf(pref, sizeof(pref), "%s", slash + 1);
    } else {
        base[0] = '\0';
        snprintf(pref, sizeof(pref), "%s", full);
    }
    DIR *d = opendir(base[0] ? base : ".");
    if (!d) return;
    struct dirent *de;
    int     cap   = 0;
    int     count = 0;
    char  **items = NULL;
    while ((de = readdir(d)) != NULL) {
        const char *name = de->d_name;
        if (name[0] == '.' && pref[0] != '.') continue;
        if (strncmp(name, pref, strlen(pref)) != 0) continue;
        int isdir = 0;
#ifdef DT_DIR
        if (de->d_type == DT_DIR) isdir = 1;
        if (de->d_type == DT_UNKNOWN)
#endif
        {
            char path[PATH_MAX];
            snprintf(path, sizeof(path), "%s%s", base[0] ? base : "", name);
            struct stat st;
            if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) isdir = 1;
        }
        char cand[PATH_MAX];
        snprintf(cand, sizeof(cand), "%s%s%s",
                 base[0] ? base : "", name, isdir ? "/" : "");
        if (count + 1 > cap) {
            cap = cap ? cap * 2 : 16;
            char **new_items = realloc(items, (size_t)cap * sizeof(char *));
            if (!new_items) {
                for (int i = 0; i < count; i++) free(items[i]);
                free(items); closedir(d); return;
            }
            items = new_items;
        }
        char *cand_copy = strdup(cand);
        if (!cand_copy) {
            for (int i = 0; i < count; i++) free(items[i]);
            free(items); closedir(d); return;
        }
        items[count++] = cand_copy;
    }
    closedir(d);
    if (count == 0) { free(items); return; }
    c->items  = items;
    c->count  = count;
    c->index  = 0;
    snprintf(c->base,   sizeof(c->base),   "%s", base);
    snprintf(c->prefix, sizeof(c->prefix), "%s", pref);
    c->active = 1;
    cmdcomp_apply_token(p, items[0]);
    ed_set_status_message("%d matches", count);
}
#pragma GCC diagnostic pop

static void cmdcomp_next(Prompt *p, CmdComp *c) {
    if (!c->active || c->count == 0) {
        cmdcomp_build_filepath(p, c);
        return;
    }
    c->index = (c->index + 1) % c->count;
    cmdcomp_apply_token(p, c->items[c->index]);
}

/* ----- vtable hooks ----------------------------------------------------- */

static const char *colon_label(Prompt *p) { (void)p; return ":"; }

static void colon_complete(Prompt *p) {
    CmdPromptState *s = p->state;
    if (cmdcomp_is_cmdname_position(p)) {
        if (s->comp.cmdname_pending) {
            /* Second Tab on the command name: hand off to fzf, seeded
             * with the partial typed so far. cmd_cpick re-fills the
             * prompt and calls prompt_keep_open(). */
            char query[128];
            int n = p->len;
            if (n >= (int)sizeof(query)) n = (int)sizeof(query) - 1;
            memcpy(query, p->buf, (size_t)n);
            query[n] = '\0';
            cmdcomp_clear(&s->comp);
            cmd_cpick(query);
        } else {
            cmdcomp_complete_cmdname(p, &s->comp);
        }
    } else {
        cmdcomp_next(p, &s->comp);
    }
}

static void colon_history(Prompt *p, int dir) {
    CmdPromptState *s = p->state;
    cmdcomp_clear(&s->comp);

    /* Plugin hooks first; first non-zero wins. */
    for (ptrdiff_t i = 0; i < arrlen(g_history_hooks); i++) {
        if (g_history_hooks[i].fn(p, dir, g_history_hooks[i].ud))
            return;
    }
    /* Built-in command-history fallback. */
    if (dir < 0) {
        char out[PROMPT_BUF_CAP];
        if (hist_browse_up(&E.history, p->buf, p->len, out, (int)sizeof(out))) {
            prompt_set_text(p, out, (int)strlen(out));
        } else {
            ed_set_status_message("No history match");
        }
    } else {
        char out[PROMPT_BUF_CAP];
        int restored = 0;
        if (hist_browse_down(&E.history, out, (int)sizeof(out), &restored)) {
            prompt_set_text(p, out, (int)strlen(out));
        }
    }
}

static PromptResult colon_on_key(Prompt *p, int key) {
    CmdPromptState *s = p->state;
    /* Any printable / backspace key invalidates the active completion
     * candidate set. The default handler doesn't know the prompt has
     * completion state, so reset it eagerly here. */
    if (key != '\t' && key != KEY_ARROW_UP && key != KEY_ARROW_DOWN &&
        key != '\r' && key != '\x1b') {
        cmdcomp_clear(&s->comp);
        hist_reset_browse(&E.history);
    }
    return prompt_default_on_key(p, key);
}

static void colon_on_submit(Prompt *p, const char *line, int len) {
    (void)p;
    if (len == 0) return; /* dispatcher will close */

    /* Parse "name args" — split on first space (in a stack copy so we
     * don't mutate p->buf, since commands may want to read the full line). */
    char work[PROMPT_BUF_CAP];
    if (len >= (int)sizeof(work)) len = (int)sizeof(work) - 1;
    memcpy(work, line, (size_t)len);
    work[len] = '\0';

    char *space    = strchr(work, ' ');
    char *cmd_name = work;
    char *cmd_args = NULL;
    if (space) { *space = '\0'; cmd_args = space + 1; }

    log_msg(":%s%s%s", cmd_name, cmd_args ? " " : "", cmd_args ? cmd_args : "");
    if (!command_execute(cmd_name, cmd_args)) {
        ed_set_status_message("Unknown command: %s", line);
        return;
    }
    /* Save full line (with args) to history. */
    regs_set_cmd(line, (size_t)len);
    hist_add(&E.history, line);
    /* If the command called prompt_keep_open(), the dispatcher will
     * not close — leaving us in MODE_COMMAND with the prefilled buf. */
}

static void colon_on_cancel(Prompt *p) {
    CmdPromptState *s = p->state;
    cmdcomp_clear(&s->comp);
    hist_reset_browse(&E.history);
}

static const PromptVTable colon_vt = {
    .label     = colon_label,
    .on_key    = colon_on_key,
    .on_submit = colon_on_submit,
    .on_cancel = colon_on_cancel,
    .complete  = colon_complete,
    .history   = colon_history,
};

void cmd_prompt_open(void) {
    /* Reset state for a fresh prompt instance. */
    cmdcomp_clear(&g_cmd_state.comp);
    prompt_open(&colon_vt, &g_cmd_state);
}

/* ===========================================================================
 * The "/" search prompt
 *
 * Far simpler: no completion, no history, just line editing plus a
 * Ctrl-R toggle for regex/literal search mode. Submit fires the search.
 * =========================================================================== */

typedef struct { int use_regex; } SearchState;
static SearchState g_search_state;

static const char *search_label(Prompt *p) {
    SearchState *s = p->state;
    return s->use_regex ? "/" : "/(lit) ";
}

static PromptResult search_on_key(Prompt *p, int key) {
    SearchState *s = p->state;
    if (key == CTRL_KEY('r')) {
        s->use_regex = !s->use_regex;
        return PROMPT_CONTINUE;
    }
    return prompt_default_on_key(p, key);
}

static void search_on_submit(Prompt *p, const char *line, int len) {
    SearchState *s = p->state;
    Buffer *buf = buf_cur();
    if (!buf) return;
    sstr_free(&E.search_query);
    E.search_query    = sstr_from(line, (size_t)len);
    E.search_is_regex = s->use_regex;
    buf_find_in(buf);
}

static const PromptVTable search_vt = {
    .label     = search_label,
    .on_key    = search_on_key,
    .on_submit = search_on_submit,
    .on_cancel = NULL,
    .complete  = NULL,
    .history   = NULL,
};

void ed_search_prompt(void) {
    if (!buf_cur()) return;
    g_search_state.use_regex = 1;
    prompt_open(&search_vt, &g_search_state);
}
