#ifndef HED_VIRTUAL_TEXT_H
#define HED_VIRTUAL_TEXT_H

#include <stddef.h>
#include "lib/sizedstr.h"

typedef struct Buffer Buffer;

/* Phase 1: end-of-line suffix only. Block placements (above/below)
 * arrive in phase 2, which will extend this enum. */
typedef enum {
    VT_PLACE_EOL = 0,
} VtPlacement;

typedef struct {
    int          ns_id;
    int          line;        /* buffer row index */
    VtPlacement  place;
    SizedStr     text;        /* owned by the table */
    const char  *sgr;         /* borrowed; expected to point at a Theme
                                 literal or other static SGR string */
    int          priority;
} VtMark;

typedef struct {
    VtMark *marks;            /* stb_ds vector; NULL when empty */
} VtTable;

/* Lifecycle. Called from buf_new / buf_close. The hook subscriptions
 * live on a process-wide registry; vtext_init only zero-inits the
 * per-buffer table and is safe to call repeatedly. */
void vtext_init(Buffer *b);
void vtext_free(Buffer *b);

/* Lazy, idempotent setup of the line-edit hook listeners. Safe to
 * call from anywhere; only the first call actually registers. */
void vtext_hooks_install_once(void);

/* Namespaces. Idempotent: a second call with the same name returns
 * the same id. Returns < 0 on failure. */
int  vtext_ns_create(const char *name);

/* Append an EOL mark to `line`. Copies `text`. If `sgr` is NULL the
 * renderer falls back to COLOR_COMMENT. Returns 0 on success. */
int  vtext_set_eol(Buffer *b, int ns, int line,
                   const char *text, size_t n, const char *sgr);

/* Drop every mark on `line` belonging to `ns`. */
int  vtext_clear_line(Buffer *b, int ns, int line);

/* Drop every mark belonging to `ns`. */
int  vtext_clear_ns(Buffer *b, int ns);

/* Drop every mark in the buffer (every namespace). */
int  vtext_clear_all(Buffer *b);

/* True if any mark exists for the buffer. Lets the renderer fast-path
 * the no-virtual-text case. */
int  vtext_buffer_has_marks(const Buffer *b);

/* Fill `out` with up to `max` EOL marks for `line`, sorted by
 * priority ascending (lower priority drawn first). Returns the count
 * written. Pointers into the returned table are valid until the next
 * mutation of the table. */
int  vtext_collect_eol(const Buffer *b, int line,
                       const VtMark **out, int max);

#endif
