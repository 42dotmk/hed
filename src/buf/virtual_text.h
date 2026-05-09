#ifndef HED_VIRTUAL_TEXT_H
#define HED_VIRTUAL_TEXT_H

#include <stddef.h>
#include "lib/sizedstr.h"

typedef struct Buffer Buffer;

/* Phase 2 (block_below) is in. Block_above is still pending; cursor
 * scroll math is harder when virtual rows precede the anchor. */
typedef enum {
    VT_PLACE_EOL = 0,
    /* One or more virtual rows drawn directly below the anchor row.
     * The mark's text may contain '\n' to occupy multiple screen rows
     * (one per logical line). The cursor cannot land on these rows. */
    VT_PLACE_BLOCK_BELOW,
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

/* Control whether marks in this namespace are wiped on every char/line
 * edit. Default is 1 (the "diagnostic" model: any edit invalidates).
 * Plugins that manage their own clears (e.g. copilot ghost text, which
 * is updated per keystroke and dismissed on cursor move / mode change)
 * set this to 0. Returns 0 on success, -1 if the ns id is unknown. */
int  vtext_ns_set_auto_clear(int ns, int auto_clear);

/* Append an EOL mark to `line`. Copies `text`. If `sgr` is NULL the
 * renderer falls back to COLOR_COMMENT. Returns 0 on success. */
int  vtext_set_eol(Buffer *b, int ns, int line,
                   const char *text, size_t n, const char *sgr);

/* Append a BLOCK_BELOW mark anchored to `line`. `text` may contain
 * '\n' separators; each segment renders as one virtual screen row
 * directly under `line`'s last visual subline. */
int  vtext_set_block_below(Buffer *b, int ns, int line,
                           const char *text, size_t n, const char *sgr);

/* Total virtual screen rows produced by every BLOCK_BELOW mark on
 * `line`. Sum of (newline-count + 1) across all such marks. Called
 * by the renderer's height accounting; should be O(marks). */
int  vtext_block_below_count(const Buffer *b, int line);

/* Look up the i-th block_below virtual row for `line` (0-based across
 * all marks on that line, in mark insertion order). Returns the
 * pointer/length into the mark's text on success and the SGR colour
 * to use, or 0 length on miss.
 *
 * `out_sgr` may be NULL if the caller doesn't need it. */
int  vtext_block_below_at(const Buffer *b, int line, int row_index,
                          const char **out_text, size_t *out_len,
                          const char **out_sgr);

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
