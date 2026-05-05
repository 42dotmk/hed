#ifndef HED_PROMPT_H
#define HED_PROMPT_H

#include "editor.h"
#include <stdbool.h>

/*
 * Prompts: pluggable one-line modal text inputs.
 *
 * MODE_COMMAND just means "a Prompt owns the keystroke stream." What
 * the prompt looks like, how it edits its line, what completion or
 * history it offers, and what happens on submit are all the prompt's
 * business — not the mode's. ":", "/", LSP rename, "save before quit?"
 * are all instances of the same primitive.
 *
 * Lifecycle: prompt_open() captures the current EditorMode for later
 * restoration and switches to MODE_COMMAND. prompt_close() restores
 * the captured mode. While the prompt is active, every keystroke
 * routed through ed_dispatch_key reaches prompt_handle_key, which
 * calls vt->on_key.
 */

typedef struct Prompt Prompt;

typedef enum {
    PROMPT_CONTINUE = 0,  /* keep prompt open, more keys coming */
    PROMPT_SUBMIT,        /* user pressed Enter — fire on_submit  */
    PROMPT_CANCEL,        /* user pressed Esc  — fire on_cancel  */
} PromptResult;

typedef struct PromptVTable {
    /* Required: text rendered before the input on the status line.
     * Returned pointer must remain valid until the next call. */
    const char *(*label)(Prompt *p);

    /* Required: process one keystroke. Most prompts can just point
     * this at prompt_default_on_key. */
    PromptResult (*on_key)(Prompt *p, int key);

    /* Optional. Called when on_key returns PROMPT_SUBMIT. The handler
     * may call prompt_keep_open() to suppress the auto-close (used by
     * commands like :c that prefill the buffer for further editing). */
    void (*on_submit)(Prompt *p, const char *line, int len);

    /* Optional. Called on PROMPT_CANCEL or external prompt_close(false). */
    void (*on_cancel)(Prompt *p);

    /* Optional. Called by prompt_default_on_key on Tab. */
    void (*complete)(Prompt *p);

    /* Optional. Called by prompt_default_on_key on Up/Down arrow.
     * dir < 0 means "older entry," dir > 0 means "newer entry." */
    void (*history)(Prompt *p, int dir);
} PromptVTable;

#define PROMPT_BUF_CAP 256

struct Prompt {
    const PromptVTable *vt;
    void               *state;        /* prompt-specific payload */
    EditorMode          return_mode;  /* mode active at prompt_open time */
    char                buf[PROMPT_BUF_CAP];
    int                 len;
    int                 cursor;       /* future: in-line editing */
    bool                stay_open;    /* set by submit handler / commands */
};

/* Lifecycle. Switches MODE_COMMAND on/off automatically. */
void    prompt_open(const PromptVTable *vt, void *state);
void    prompt_close(bool submitted);
bool    prompt_active(void);
Prompt *prompt_current(void);

/* Run one keystroke through the active prompt. No-op if none. */
void prompt_handle_key(int key);

/*
 * Default line-editor on_key. Suitable verbatim for prompts that need
 * standard line behaviour:
 *   Enter        -> PROMPT_SUBMIT
 *   Esc          -> PROMPT_CANCEL
 *   Backspace    -> remove last byte
 *   Tab          -> vt->complete(p)  (if non-NULL)
 *   Up / Down    -> vt->history(p, ±1) (if non-NULL)
 *   printable    -> append to buf
 * Prompts that need extra keys (e.g. search's Ctrl-R regex toggle)
 * intercept those and delegate the rest here.
 */
PromptResult prompt_default_on_key(Prompt *p, int key);

/* Helpers usable from on_submit handlers and the commands those
 * handlers run. */
void prompt_keep_open(void);                       /* don't auto-close */
void prompt_set_text(Prompt *p, const char *s, int len);

#endif
