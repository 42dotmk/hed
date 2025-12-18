#include "fold.h"
#include <stdlib.h>
#include <string.h>

#define FOLD_INITIAL_CAPACITY 16

void fold_list_init(FoldList *list) {
    if (!list)
        return;
    list->regions = NULL;
    list->count = 0;
    list->capacity = 0;
}

void fold_list_free(FoldList *list) {
    if (!list)
        return;
    if (list->regions) {
        free(list->regions);
        list->regions = NULL;
    }
    list->count = 0;
    list->capacity = 0;
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
}

void fold_remove_region(FoldList *list, int idx) {
    if (!list || idx < 0 || idx >= list->count)
        return;

    /* Shift remaining elements down */
    for (int i = idx; i < list->count - 1; i++) {
        list->regions[i] = list->regions[i + 1];
    }
    list->count--;
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

    list->regions[idx].is_collapsed = !list->regions[idx].is_collapsed;
    return true;
}

bool fold_collapse_at_line(FoldList *list, int line) {
    int idx = fold_find_at_line(list, line);
    if (idx == -1)
        return false;

    list->regions[idx].is_collapsed = true;
    return true;
}

bool fold_expand_at_line(FoldList *list, int line) {
    int idx = fold_find_at_line(list, line);
    if (idx == -1)
        return false;

    list->regions[idx].is_collapsed = false;
    return true;
}

bool fold_is_line_hidden(const FoldList *list, int line) {
    if (!list || line < 0)
        return false;

    /* A line is hidden if it's inside a collapsed fold and not on the start
     * line */
    for (int i = 0; i < list->count; i++) {
        FoldRegion *r = &list->regions[i];
        if (r->is_collapsed && line > r->start_line && line <= r->end_line) {
            return true;
        }
    }

    return false;
}

int fold_get_visible_line_count(const FoldList *list, int total_lines) {
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
}
