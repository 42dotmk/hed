#include "buf/attrspan.h"
#include "stb_ds.h"
#include <stdlib.h>
#include <string.h>

static void attrspan_free_index(AttrSpans *s) {
    free(s->row_first);
    free(s->row_count);
    s->row_first     = NULL;
    s->row_count     = NULL;
    s->row_index_len = 0;
}

void attrspan_init(AttrSpans *s) {
    if (!s) return;
    s->items         = NULL;
    s->sorted        = 0;
    s->row_first     = NULL;
    s->row_count     = NULL;
    s->row_index_len = 0;
}

void attrspan_free(AttrSpans *s) {
    if (!s) return;
    arrfree(s->items);
    s->items  = NULL;
    s->sorted = 0;
    attrspan_free_index(s);
}

void attrspan_clear(AttrSpans *s) {
    if (!s) return;
    /* arrsetlen(items, 0) keeps the backing storage warm across frames. */
    if (s->items)
        arrsetlen(s->items, 0);
    s->sorted = 0;
    attrspan_free_index(s);
}

void attrspan_push(AttrSpans *s, int row, int col_start, int col_end,
                   const char *sgr, int priority) {
    if (!s || !sgr || col_end <= col_start || row < 0)
        return;
    AttrSpan span = {
        .row       = row,
        .col_start = col_start,
        .col_end   = col_end,
        .sgr       = sgr,
        .priority  = priority,
    };
    arrput(s->items, span);
    s->sorted = 0;
}

static int span_cmp(const void *a, const void *b) {
    const AttrSpan *x = a, *y = b;
    if (x->row != y->row)               return x->row < y->row ? -1 : 1;
    if (x->col_start != y->col_start)   return x->col_start < y->col_start ? -1 : 1;
    if (x->priority != y->priority)     return x->priority > y->priority ? -1 : 1;
    return 0;
}

void attrspan_sort(AttrSpans *s) {
    if (!s || s->sorted) return;
    size_t n = s->items ? (size_t)arrlen(s->items) : 0;
    if (n > 1)
        qsort(s->items, n, sizeof(AttrSpan), span_cmp);

    /* Build the per-row index. Spans are sorted by row asc, so each
     * row's spans occupy a contiguous range. */
    attrspan_free_index(s);
    if (n > 0) {
        int max_row = 0;
        for (size_t i = 0; i < n; i++)
            if (s->items[i].row > max_row) max_row = s->items[i].row;
        s->row_index_len = max_row + 1;
        s->row_first = calloc((size_t)s->row_index_len, sizeof(int));
        s->row_count = calloc((size_t)s->row_index_len, sizeof(int));
        if (!s->row_first || !s->row_count) {
            attrspan_free_index(s);
        } else {
            for (int r = 0; r < s->row_index_len; r++)
                s->row_first[r] = -1;
            for (size_t i = 0; i < n; i++) {
                int r = s->items[i].row;
                if (s->row_first[r] < 0) s->row_first[r] = (int)i;
                s->row_count[r]++;
            }
        }
    }
    s->sorted = 1;
}

const AttrSpan *attrspan_at(const AttrSpans *s, int row, int col) {
    if (!s || !s->items) return NULL;
    /* Fast path: row-indexed lookup after sort. */
    if (s->sorted && s->row_first && row >= 0 && row < s->row_index_len) {
        int first = s->row_first[row];
        if (first < 0) return NULL;
        int cnt = s->row_count[row];
        const AttrSpan *best = NULL;
        for (int i = first; i < first + cnt; i++) {
            const AttrSpan *sp = &s->items[i];
            if (sp->col_start > col) break;  /* sorted by col_start asc */
            if (col >= sp->col_end) continue;
            if (!best || sp->priority > best->priority)
                best = sp;
        }
        return best;
    }
    /* Pre-sort fallback. */
    const AttrSpan *best = NULL;
    int n = (int)arrlen(s->items);
    for (int i = 0; i < n; i++) {
        const AttrSpan *sp = &s->items[i];
        if (sp->row != row) continue;
        if (col < sp->col_start || col >= sp->col_end) continue;
        if (!best || sp->priority > best->priority)
            best = sp;
    }
    return best;
}
