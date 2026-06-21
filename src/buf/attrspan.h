#ifndef HED_ATTRSPAN_H
#define HED_ATTRSPAN_H

/*
 * AttrSpan — attributed runs over buffer bytes, applied at render time.
 *
 * Phase-1 scaffolding for the renderer abstraction: each frame the
 * renderer fires HOOK_RENDER_PRE on every visible buffer, and any
 * highlighter plugin appends spans saying "bytes [col_start, col_end)
 * on this row carry this attribute." The renderer walks the row and
 * emits transitions; multiple plugins can stack overlays by priority.
 *
 * The attribute payload is a borrowed ANSI SGR string for now, so the
 * existing theme tokens (`COLOR_KEYWORD` and friends in lib/theme.h)
 * plug in unchanged. Phase 2 swaps `sgr` for a typed Attr struct and
 * the renderer becomes responsible for translating to backend bytes.
 */

#include <stddef.h>

typedef struct Buffer Buffer;

typedef struct {
    int         row;        /* buffer row (file row index) */
    int         col_start;  /* inclusive, in chars-space byte offset */
    int         col_end;    /* exclusive */
    const char *sgr;        /* borrowed; lifetime must outlive the frame */
    int         priority;   /* higher wins on overlap; ties: insertion order */
} AttrSpan;

typedef struct {
    AttrSpan *items;          /* stb_ds vector; NULL when empty */
    int       sorted;         /* 1 once attrspan_sort has run for this frame */
    /* Row index built by attrspan_sort: row_first[r] is the index of
     * the first span on row r in `items`; row_count[r] is the count.
     * Lets per-byte lookups skip straight to the row's spans instead
     * of scanning the whole vector. Indexed by row 0..row_index_len. */
    int      *row_first;
    int      *row_count;
    int       row_index_len;
} AttrSpans;

/* Lifecycle. Called from buf_new / buf_close. */
void attrspan_init(AttrSpans *s);
void attrspan_free(AttrSpans *s);

/* Drop every span in the table. Called by the renderer at the start of
 * each frame, before HOOK_RENDER_PRE fires. */
void attrspan_clear(AttrSpans *s);

/* Append a span. Caller is responsible for ensuring `sgr` lives long
 * enough — same contract as VtMark.sgr. */
void attrspan_push(AttrSpans *s, int row, int col_start, int col_end,
                   const char *sgr, int priority);

/* Stable-sort by (row asc, col_start asc, -priority desc) so that a
 * left-to-right walk meets each span in render order and the
 * highest-priority overlap wins by being seen first. */
void attrspan_sort(AttrSpans *s);

/* Find the span covering (row, col) with the highest priority among
 * those that match. Returns NULL if no span covers it. O(log n + k)
 * once `s` is sorted; otherwise linear. */
const AttrSpan *attrspan_at(const AttrSpans *s, int row, int col);

#endif /* HED_ATTRSPAN_H */
