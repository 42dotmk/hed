#ifndef TS_H
#define TS_H

#include "buf/buffer.h"
#include <stddef.h>

/* Global toggle */
void ts_set_enabled(int on);
int ts_is_enabled(void);

/* Seed the highlight registry (see highlight.h) with the bundled
 * theme.h palette. Idempotent: re-seeding restores defaults for any
 * roles a plugin may have overridden. */
void ts_seed_default_theme(void);

/* Register an embedded tree-sitter query for a language. qname is the
 * conventional file name ("highlights.scm", "injections.scm"); content
 * is the query source. The pointer is borrowed — the caller's string
 * must outlive the editor process (typically a static const char[]).
 *
 * Lookup order during ts_buffer_load_language:
 *   1. ~/.config/hed/ts/queries.local/<lang>/<qname>  (handwritten override)
 *   2. plugin-registered string for (lang, qname)      (this registry)
 *   3. ~/.config/hed/ts/queries/<lang>/<qname>         (tsi-installed)
 *   4. ./queries/<lang>/<qname>                        (cwd-local, dev)
 *
 * Returns 0 on success. Declared weak so plugins outside the treesitter
 * plugin can guard their calls. */
int ts_register_query(const char *lang_name, const char *qname,
                      const char *content) __attribute__((weak));

/* Per-buffer lifecycle. State lives in a plugin-internal map keyed
 * by Buffer*; ts_buffer_autoload and ts_buffer_reparse below create
 * the entry on demand. Cleanup happens via a HOOK_BUFFER_CLOSE
 * handler registered in treesitter_init. */
void ts_buffer_reparse(Buffer *buf);

/* Hook handlers exposed to treesitter_init for registration.
 * Forward-declared so ts.h doesn't need to pull in hooks.h's full
 * struct definitions (already brought in transitively by hed.h
 * everywhere ts.h is used today). */
struct HookBufferEvent;
void ts_on_buffer_open(struct HookBufferEvent *e);
void ts_on_buffer_close(struct HookBufferEvent *e);
/* Try to load language for buffer based on path or explicit name */
int ts_buffer_load_language(Buffer *buf, const char *lang_name);
/* Attempt autoload by filename/filetype */
int ts_buffer_autoload(Buffer *buf);

/* HOOK_RENDER_PRE handler: walks the whole-buffer tree-sitter parse,
 * splits each query capture by line, and pushes one AttrSpan per
 * (capture, row) into event->spans. Tree-sitter sees the full
 * document anyway, so collecting for the whole buffer once per frame
 * is no more parse-work than per-row and gives correct highlighting
 * for tokens that straddle the visible viewport. Forward-declared so
 * the treesitter init code can register it without exposing
 * HookRenderEvent's full layout here. */
struct HookRenderEvent;
void ts_render_pre_hook(const struct HookRenderEvent *event);

#endif /* TS_H */
