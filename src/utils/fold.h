#ifndef FOLD_H
#define FOLD_H

#include <stdbool.h>

/*
 * Code Folding System
 * ===================
 *
 * Folding is line-based. Each fold region has a start line and end line.
 * Folds can be nested, but if a parent fold is collapsed, child folds
 * are not processed during rendering.
 */

/* Fold region structure - represents a collapsible region in the buffer */
typedef struct FoldRegion {
    int start_line;     /* Line where fold starts (inclusive) */
    int end_line;       /* Line where fold ends (inclusive) */
    bool is_collapsed;  /* Whether this fold is currently collapsed */
} FoldRegion;

/* Fold list - dynamic array of fold regions for a buffer */
typedef struct FoldList {
    FoldRegion *regions; /* Array of fold regions */
    int count;           /* Number of active folds */
    int capacity;        /* Allocated capacity */
} FoldList;

/* Initialize a new fold list */
void fold_list_init(FoldList *list);

/* Free fold list resources */
void fold_list_free(FoldList *list);

/* Add a new fold region (start_line to end_line, initially expanded) */
void fold_add_region(FoldList *list, int start_line, int end_line);

/* Remove fold region at given index */
void fold_remove_region(FoldList *list, int idx);

/* Find fold region that contains the given line (returns index or -1) */
int fold_find_at_line(const FoldList *list, int line);

/* Toggle fold at the given line (collapse if expanded, expand if collapsed) */
bool fold_toggle_at_line(FoldList *list, int line);

/* Collapse fold at the given line */
bool fold_collapse_at_line(FoldList *list, int line);

/* Expand fold at the given line */
bool fold_expand_at_line(FoldList *list, int line);

/* Check if a line is hidden due to folding */
bool fold_is_line_hidden(const FoldList *list, int line);

/* Get the visible line count (accounting for collapsed folds) */
int fold_get_visible_line_count(const FoldList *list, int total_lines);

/* Clear all folds */
void fold_clear_all(FoldList *list);

#endif /* FOLD_H */
