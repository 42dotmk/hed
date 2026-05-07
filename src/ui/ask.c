#include "ui/ask.h"
#include "editor.h"
#include "prompt.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    char        *question;   /* heap copy, owned */
    AskCallback  cb;
    void        *ud;
} AskState;

static const char *ask_label(Prompt *p) {
    AskState *s = p->state;
    return s ? s->question : "";
}

static void ask_finish(Prompt *p, const char *answer) {
    AskState *s = p->state;
    if (!s) return;
    /* Detach state from the prompt before invoking the callback so
     * the callback can call ask() again to chain another question. */
    p->state = NULL;
    AskCallback cb = s->cb;
    void       *ud = s->ud;
    free(s->question);
    free(s);
    if (cb) cb(answer, ud);
}

static void ask_on_submit(Prompt *p, const char *line, int len) {
    /* `line` points into p->buf; copy onto the stack so the callback
     * sees a stable C string even if it runs another ask. */
    char copy[PROMPT_BUF_CAP];
    if (len < 0) len = 0;
    if (len >= (int)sizeof(copy)) len = (int)sizeof(copy) - 1;
    memcpy(copy, line, (size_t)len);
    copy[len] = '\0';
    ask_finish(p, copy);
}

static void ask_on_cancel(Prompt *p) {
    ask_finish(p, NULL);
}

static const PromptVTable ask_vt = {
    .label     = ask_label,
    .on_key    = prompt_default_on_key,
    .on_submit = ask_on_submit,
    .on_cancel = ask_on_cancel,
    .complete  = NULL,
    .history   = NULL,
};

void ask(const char *question, const char *initial,
         AskCallback cb, void *ud) {
    if (prompt_active()) {
        ed_set_status_message("ask: a prompt is already open");
        if (cb) cb(NULL, ud);
        return;
    }

    AskState *s = calloc(1, sizeof(*s));
    if (!s) {
        if (cb) cb(NULL, ud);
        return;
    }
    s->question = strdup(question ? question : "");
    s->cb       = cb;
    s->ud       = ud;
    if (!s->question) {
        free(s);
        if (cb) cb(NULL, ud);
        return;
    }

    prompt_open(&ask_vt, s);
    if (initial && *initial) {
        Prompt *p = prompt_current();
        if (p) prompt_set_text(p, initial, (int)strlen(initial));
    }
}
