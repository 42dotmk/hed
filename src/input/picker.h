#ifndef HED_PICKER_H
#define HED_PICKER_H

/*
 * picker — name-keyed registry of interactive pickers.
 *
 * A "picker" is whatever a plugin wants it to be: an fzf invocation,
 * an inline list, a modal window. From core's point of view it's just
 * "given a name and a query seed, present a UI and act on the result."
 *
 * Lookup is by short name ("command", "buffers", "files", ...). The
 * pickers plugin registers fzf-backed implementations; other plugins
 * (or future Windows backends) can register replacements. Last write
 * wins so a user override beats a plugin default.
 *
 * Core commands that need to pick something call picker_invoke and
 * fall back to a sensible non-interactive behaviour when no picker is
 * registered. This keeps core free of external-tool dependencies —
 * `:b`, `:commands`, `gF` still work in a build without fzf, they
 * just degrade to a status-line list.
 */

/* The picker callback. `seed` is the initial query (may be NULL or
 * empty). The picker owns everything that happens after — UI, source
 * enumeration, parsing the selection, dispatching the action. */
typedef void (*PickerFn)(const char *seed);

/* Register `fn` under `name`. Re-registering replaces the previous
 * entry. Pass NULL `fn` to unregister. */
void picker_register(const char *name, PickerFn fn);

/* Look up a picker by name. Returns NULL if none registered. */
PickerFn picker_get(const char *name);

/* Invoke the picker for `name` with `seed`. Returns 1 if a picker
 * was found and called, 0 otherwise — callers use the 0 return as
 * the cue to fall back. */
int picker_invoke(const char *name, const char *seed);

/*
 * picker_list — generic list picker. Plugins owning their data
 * (mail attachments, tmux panes, ts themes, ...) feed in a list
 * of strings and get back the user's selection, without needing
 * to know which UI (fzf, modal window, ...) actually presents it.
 *
 * The pickers plugin registers an fzf-backed implementation at
 * init; other plugins can replace it. Last write wins.
 *
 * items     — read-only array of `count` C-strings (not freed).
 * multi     — 1 to allow multi-select, 0 for single.
 * out_lines — receives a heap-allocated array of heap-allocated
 *             strings holding the selected items. NULL/0 on cancel.
 *             Free with picker_list_free.
 * out_count — receives number of selected items.
 *
 * Returns 1 if an implementation ran (including 0-selection cancel),
 *         0 if no implementation is registered or the backend failed.
 * Callers use the 0 return to know they should fall back.
 */
typedef int (*PickerListFn)(const char **items, int count, int multi,
                            char ***out_lines, int *out_count);

void picker_list_register(PickerListFn fn);
int  picker_list(const char **items, int count, int multi,
                 char ***out_lines, int *out_count);
void picker_list_free(char **lines, int count);

#endif /* HED_PICKER_H */
