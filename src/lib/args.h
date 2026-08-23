#ifndef ARGS_H
#define ARGS_H

#include <stddef.h>

/* Helpers for parsing :command argument strings. Stateless and
 * NULL-safe — every plugin used to hand-roll these. */

/* Skip leading ASCII whitespace; returns a pointer into s (NULL in,
 * NULL out). */
const char *args_skip_ws(const char *s);

/* Copy the next whitespace-delimited token into dst (NUL-terminated,
 * truncated at dst_sz-1) and return a pointer just past it in s. dst
 * is "" when s is NULL or exhausted. */
const char *args_next_token(const char *s, char *dst, size_t dst_sz);

/* Parse the classic on|off|toggle argument. NULL/empty args and
 * "toggle" flip `cur`; "on"/"1" force 1, "off"/"0" force 0. Returns
 * the new state, or -1 for anything else (caller prints its usage
 * line). */
int args_tristate(const char *args, int cur);

/* One verb of a :command subcommand table. The callback signature
 * matches CommandCallback, so plain cmd_* functions can be rows. */
typedef struct ArgVerb {
    const char *name;             /* "" matches empty args */
    void (*fn)(const char *rest); /* rest: args after the verb, ws-skipped */
} ArgVerb;

/* Match the first token of `args` against `verbs` and invoke the
 * winner with the remainder. Returns 1 when dispatched, 0 when no
 * verb matched (caller prints its usage line). */
int args_dispatch(const char *args, const ArgVerb *verbs, int n);

#endif /* ARGS_H */
