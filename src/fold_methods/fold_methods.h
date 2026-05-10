#ifndef FOLD_METHODS_H
#define FOLD_METHODS_H

#include "buf/buffer.h"

/*
 * FOLD METHOD REGISTRY
 * ====================
 *
 * A "fold method" is a named function that scans a buffer and emits
 * fold regions. Built-ins are "manual" (no-op), "bracket" (matches
 * `{` / `}`), and "indent" (indentation levels). Plugins extend the
 * registry with `fold_method_register` — e.g. the markdown plugin
 * registers "markdown" to fold by heading level.
 *
 * The active method per buffer is stored as a name on
 * `Buffer.fold_method`. A filetype default map
 * (`fold_method_set_default`) lets configs say "use markdown folding
 * for filetype markdown" — the BUFFER_OPEN hook installed by
 * `fold_method_init` consults the map when the buffer has no explicit
 * method yet. The user can override at any time via `:foldmethod`.
 *
 * Registration semantics mirror commands and keybinds: last-write-
 * wins on the name, so plugin defaults stay overridable from config.
 */

typedef void (*FoldDetectFn)(Buffer *buf);

/* Initialise the registry, register the built-in methods, and install
 * the BUFFER_OPEN hook that applies filetype defaults. Called once
 * from ed_init before config_init. */
void fold_method_init(void);

/* Register (or replace) a fold method by name. Passing fn=NULL is
 * legal and registers a no-op detector — useful for "manual"-style
 * methods that should not auto-detect. */
void fold_method_register(const char *name, FoldDetectFn fn);

/* Look up a fold method by name. Returns NULL if not registered. */
FoldDetectFn fold_method_lookup(const char *name);

/* Apply a fold method to a buffer by name. NULL or unknown name is a
 * no-op (existing folds are preserved). */
void fold_apply_method(Buffer *buf, const char *name);

/* Filetype → default-method-name map. Last-write-wins per filetype.
 * Pass method_name=NULL to clear the default for `filetype`. */
void fold_method_set_default(const char *filetype, const char *method_name);
const char *fold_method_get_default(const char *filetype);

/* Iteration helpers for `:foldmethod` listing / completion. */
int fold_method_count(void);
const char *fold_method_name_at(int idx);

/* Built-in detectors. Exposed so plugins can compose them or invoke
 * directly without going through the registry. */
void fold_detect_brackets(Buffer *buf);
void fold_detect_indent(Buffer *buf);

#endif /* FOLD_METHODS_H */
