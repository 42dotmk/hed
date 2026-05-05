#include "prompt.h"
#include "editor.h"
#include "terminal.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

static Prompt g_prompt;

void prompt_open(const PromptVTable *vt, void *state) {
    /* Capture the prior edit mode so prompt_close can restore it.
     * This makes prompts self-contained — callers don't manage modes. */
    EditorMode prior = E.mode;
    g_prompt.vt          = vt;
    g_prompt.state       = state;
    g_prompt.return_mode = prior;
    g_prompt.buf[0]      = '\0';
    g_prompt.len         = 0;
    g_prompt.cursor      = 0;
    g_prompt.stay_open   = false;
    if (E.mode != MODE_COMMAND) ed_set_mode(MODE_COMMAND);
}

void prompt_close(bool submitted) {
    if (!g_prompt.vt) return;
    /* Snapshot vt/state on the stack so on_cancel sees its own data.
     * Then null the live slot before calling on_cancel — that way, if
     * on_cancel itself opens a new prompt via prompt_open, our cleanup
     * doesn't clobber the new state. */
    Prompt              snap    = g_prompt;
    EditorMode          restore = g_prompt.return_mode;
    g_prompt.vt    = NULL;
    g_prompt.state = NULL;
    if (!submitted && snap.vt->on_cancel) snap.vt->on_cancel(&snap);
    /* If on_cancel reopened a prompt, leave the new mode alone. */
    if (!g_prompt.vt && E.mode == MODE_COMMAND)
        ed_set_mode(restore);
}

bool    prompt_active(void)  { return g_prompt.vt != NULL; }
Prompt *prompt_current(void) { return g_prompt.vt ? &g_prompt : NULL; }

void prompt_keep_open(void) { g_prompt.stay_open = true; }

void prompt_set_text(Prompt *p, const char *s, int len) {
    if (!p) return;
    if (len < 0) len = 0;
    if (len > (int)sizeof(p->buf) - 1) len = (int)sizeof(p->buf) - 1;
    if (s && len) memcpy(p->buf, s, (size_t)len);
    p->len           = len;
    p->buf[p->len]   = '\0';
    p->cursor        = len;
}

void prompt_handle_key(int key) {
    if (!g_prompt.vt) return;
    PromptResult r = g_prompt.vt->on_key(&g_prompt, key);
    switch (r) {
    case PROMPT_CONTINUE:
        return;
    case PROMPT_SUBMIT:
        g_prompt.stay_open = false;
        if (g_prompt.vt->on_submit)
            g_prompt.vt->on_submit(&g_prompt, g_prompt.buf, g_prompt.len);
        /* on_submit may have closed/reopened the prompt or set stay_open. */
        if (g_prompt.vt && !g_prompt.stay_open)
            prompt_close(true);
        return;
    case PROMPT_CANCEL:
        prompt_close(false);
        return;
    }
}

PromptResult prompt_default_on_key(Prompt *p, int key) {
    if (key == '\r')   return PROMPT_SUBMIT;
    if (key == '\x1b') return PROMPT_CANCEL;
    if (key == KEY_DELETE || key == CTRL_KEY('h')) {
        if (p->len > 0) p->len--;
        p->buf[p->len] = '\0';
        p->cursor = p->len;
        return PROMPT_CONTINUE;
    }
    if (key == '\t') {
        if (p->vt->complete) p->vt->complete(p);
        return PROMPT_CONTINUE;
    }
    if (key == KEY_ARROW_UP) {
        if (p->vt->history) p->vt->history(p, -1);
        return PROMPT_CONTINUE;
    }
    if (key == KEY_ARROW_DOWN) {
        if (p->vt->history) p->vt->history(p, +1);
        return PROMPT_CONTINUE;
    }
    if (!iscntrl(key) && key < 128) {
        if (p->len < (int)sizeof(p->buf) - 1) {
            p->buf[p->len++] = (char)key;
            p->buf[p->len]   = '\0';
            p->cursor        = p->len;
        }
    }
    return PROMPT_CONTINUE;
}
