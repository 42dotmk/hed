#ifndef HED_UI_ASK_H
#define HED_UI_ASK_H

/* Reusable one-line status-bar prompt for asking the user something.
 *
 * Thin wrapper over src/prompt.{c,h}: instead of writing a fresh
 * PromptVTable for each yes/no or choice question, callers hand a
 * label, an optional pre-fill, and a callback. The callback fires
 * with the user's typed answer on Enter, or with NULL on Esc.
 *
 * The caller parses the answer string however it likes — "y"/"n",
 * a choice index, freeform text. ask() makes no policy calls.
 *
 * Only one prompt may be active at a time (the prompt module
 * enforces this); a second ask() while one is open is a no-op
 * with a status message. */

typedef void (*AskCallback)(const char *answer, void *ud);

void ask(const char *question, const char *initial,
         AskCallback cb, void *ud);

#endif
