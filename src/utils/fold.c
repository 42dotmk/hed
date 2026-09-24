#include "utils/fold.h"
#include "buf/buffer.h"
#include <stdlib.h>
#include <string.h>

#define FOLD_INITIAL_CAPACITY 16

void fold_list_init(FoldList *list) {
    if (!list)
        return;
    list->regions = NULL;
    list->count = 0;
    list->capacity = 0;
    list->hidden = NULL;
    list->hidden_n = 0;
    list->hidden_stale = false;
}

void fold_list_free(FoldList *list) {
    if (!list)
        return;
    if (list->regions) {
        free(list->regions);
        list->regions = NULL;
    }
    free(list->hidden);
    list->hidden = NULL;
    list->hidden_n = 0;
    list->hidden_stale = false;
    list->count = 0;
    list->capacity = 0;
}

void fold_invalidate(FoldList *list) {
    if (list)
        list->hidden_stale = true;
}

void fold_set_collapsed(FoldList *list, int idx, bool collapsed) {
    if (!list || idx < 0 || idx >= list->count)
        return;
    list->regions[idx].is_collapsed = collapsed;
    list->hidden_stale = true;
}

static void fold_list_ensure_capacity(FoldList *list) {
    if (!list)
        return;

    if (list->count >= list->capacity) {
        int new_capacity =
            (list->capacity == 0) ? FOLD_INITIAL_CAPACITY : list->capacity * 2;
        FoldRegion *new_regions =
            realloc(list->regions, new_capacity * sizeof(FoldRegion));
        if (!new_regions)
            return; /* Out of memory */
        list->regions = new_regions;
        list->capacity = new_capacity;
    }
}

void fold_add_region(FoldList *list, int start_line, int end_line) {
    if (!list || start_line < 0 || end_line < start_line)
        return;

    fold_list_ensure_capacity(list);
    if (list->count >= list->capacity)
        return; /* Failed to allocate */

    list->regions[list->count].start_line = start_line;
    list->regions[list->count].end_line = end_line;
    list->regions[list->count].is_collapsed = false;
    list->count++;
    list->hidden_stale = true;
}

void fold_remove_region(FoldList *list, int idx) {
    if (!list || idx < 0 || idx >= list->count)
        return;

    /* Shift remaining elements down */
    for (int i = idx; i < list->count - 1; i++) {
        list->regions[i] = list->regions[i + 1];
    }
    list->count--;
    list->hidden_stale = true;
}

int fold_find_at_line(const FoldList *list, int line) {
    if (!list || line < 0)
        return -1;

    /* Find the innermost fold containing this line */
    int best_idx = -1;
    int best_size = -1;

    for (int i = 0; i < list->count; i++) {
        FoldRegion *r = &list->regions[i];
        if (line >= r->start_line && line <= r->end_line) {
            int size = r->end_line - r->start_line;
            if (best_idx == -1 || size < best_size) {
                best_idx = i;
                best_size = size;
            }
        }
    }

    return best_idx;
}

bool fold_toggle_at_line(FoldList *list, int line) {
    int idx = fold_find_at_line(list, line);
    if (idx == -1)
        return false;

    fold_set_collapsed(list, idx, !list->regions[idx].is_collapsed);
    return true;
}

bool fold_collapse_at_line(FoldList *list, int line) {
    int idx = fold_find_at_line(list, line);
    if (idx == -1)
        return false;

    fold_set_collapsed(list, idx, true);
    return true;
}

bool fold_expand_at_line(FoldList *list, int line) {
    int idx = fold_find_at_line(list, line);
    if (idx == -1)
        return false;

    fold_set_collapsed(list, idx, false);
    return true;
}

static const FoldList *g_sort_list;

/* Outer regions first: by start, then longer (later end) first. */
static int cmp_region_idx(const void *a, const void *b) {
    const FoldRegion *x = &g_sort_list->regions[*(const int *)a];
    const FoldRegion *y = &g_sort_list->regions[*(const int *)b];
    if (x->start_line != y->start_line)
        return (x->start_line > y->start_line) -
               (x->start_line < y->start_line);
    return (x->end_line < y->end_line) - (x->end_line > y->end_line);
}

void fold_apply_level(FoldList *list, int level) {
    if (!list || list->count == 0)
        return;
    /* Vim foldlevel semantics: folds deeper than `level` close, the rest
     * open. level 0 collapses everything to top-level summaries; a large
     * level (e.g. 100) opens every fold.
     *
     * A region's level is one more than the number of regions that
     * strictly contain it (an identical span does not count). Walk the
     * regions outer-first with a stack of the open enclosing ones —
     * O(n log n) instead of comparing every pair. Assumes regions nest
     * (every fold method produces nested spans). */
    int n = list->count;
    int *order = malloc((size_t)n * sizeof(int));
    int *stack = malloc((size_t)n * sizeof(int));
    int *lvl = malloc((size_t)n * sizeof(int));
    if (!order || !stack || !lvl) {
        free(order);
        free(stack);
        free(lvl);
        return;
    }
    for (int i = 0; i < n; i++)
        order[i] = i;
    g_sort_list = list;
    qsort(order, (size_t)n, sizeof(int), cmp_region_idx);
    int sp = 0;
    for (int k = 0; k < n; k++) {
        int i = order[k];
        const FoldRegion *r = &list->regions[i];
        /* What's left on the stack encloses r; copies of r's own span
         * sit on top and don't count (each other copy does, as for
         * any container). */
        while (sp > 0 && list->regions[stack[sp - 1]].end_line < r->end_line)
            sp--;
        int same = 0;
        while (same < sp) {
            const FoldRegion *t = &list->regions[stack[sp - 1 - same]];
            if (t->start_line != r->start_line || t->end_line != r->end_line)
                break;
            same++;
        }
        lvl[i] = 1 + sp - same;
        stack[sp++] = i;
    }
    for (int i = 0; i < n; i++)
        list->regions[i].is_collapsed = lvl[i] > level;
    list->hidden_stale = true;
    free(order);
    free(stack);
    free(lvl);
}

static int cmp_span(const void *a, const void *b) {
    const int *x = a, *y = b;
    return (x[0] > y[0]) - (x[0] < y[0]);
}

/* Rebuild list->hidden: each collapsed region hides (start, end], so
 * collect those spans, sort by start and merge overlaps. */
static void fold_rebuild_hidden(FoldList *list) {
    list->hidden_stale = false;
    list->hidden_n = 0;
    int n = 0;
    for (int i = 0; i < list->count; i++)
        if (list->regions[i].is_collapsed &&
            list->regions[i].end_line > list->regions[i].start_line)
            n++;
    if (n == 0)
        return;
    int *spans = realloc(list->hidden, (size_t)n * 2 * sizeof(int));
    if (!spans)
        return;
    list->hidden = spans;
    n = 0;
    for (int i = 0; i < list->count; i++) {
        const FoldRegion *r = &list->regions[i];
        if (r->is_collapsed && r->end_line > r->start_line) {
            spans[2 * n] = r->start_line + 1;
            spans[2 * n + 1] = r->end_line;
            n++;
        }
    }
    qsort(spans, (size_t)n, 2 * sizeof(int), cmp_span);
    int m = 0;
    for (int i = 0; i < n; i++) {
        if (m > 0 && spans[2 * i] <= spans[2 * (m - 1) + 1] + 1) {
            if (spans[2 * i + 1] > spans[2 * (m - 1) + 1])
                spans[2 * (m - 1) + 1] = spans[2 * i + 1];
        } else {
            spans[2 * m] = spans[2 * i];
            spans[2 * m + 1] = spans[2 * i + 1];
            m++;
        }
    }
    list->hidden_n = m;
}

bool fold_is_line_hidden(FoldList *list, int line) {
    if (!list || line < 0)
        return false;
    if (list->hidden_stale)
        fold_rebuild_hidden(list);

    /* A line is hidden if it's inside a collapsed fold and not on the start
     * line: find the last span starting at or before it. */
    int lo = 0, hi = list->hidden_n - 1, found = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (list->hidden[2 * mid] <= line) {
            found = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return found >= 0 && line <= list->hidden[2 * found + 1];
}

int fold_get_visible_line_count(FoldList *list, int total_lines) {
    if (!list || total_lines <= 0)
        return total_lines;

    int visible = 0;
    for (int line = 0; line < total_lines; line++) {
        if (!fold_is_line_hidden(list, line)) {
            visible++;
        }
    }

    return visible;
}

void fold_clear_all(FoldList *list) {
    if (!list)
        return;
    list->count = 0;
    list->hidden_stale = true;
}

void fold_reset_buffer(struct Buffer *buf) {
    if (!buf)
        return;
    fold_clear_all(&buf->folds);
    for (int i = 0; i < buf->num_rows; i++) {
        buf->rows[i].fold_start = false;
        buf->rows[i].fold_end = false;
    }
}
