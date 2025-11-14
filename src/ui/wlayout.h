#ifndef WLAYOUT_H
#define WLAYOUT_H

#include "abuf.h"

/* Split direction for layout nodes */
typedef enum {
    WL_SINGLE = 0,
    WL_HORIZONTAL = 1, /* stacked: children arranged top-to-bottom */
    WL_VERTICAL = 2    /* side-by-side: children arranged left-to-right */
} WSplitDir;

/* Decoration characters for borders/splits */
typedef struct {
    const char *v;     /* vertical line (UTF-8) */
    const char *h;     /* horizontal line (UTF-8) */
    const char *tl, *tr;/* top-left, top-right corners */
    const char *bl, *br;/* bottom-left, bottom-right corners */
    const char *tee_t, *tee_b, *tee_l, *tee_r; /* tees */
    const char *cross; /* intersection */
} WDecorChars;

/* Decorations configuration */
typedef struct {
    int enabled;     /* 1=draw borders/splits, 0=off */
    int thickness;   /* thickness for borders and split bars (>=1) */
    unsigned top:1;
    unsigned bottom:1;
    unsigned left:1;
    unsigned right:1;
    WDecorChars chars;
} WDecor;

/* Layout tree node (opaque to most modules) */
typedef struct WLayoutNode {
    struct WLayoutNode *parent;
    WSplitDir dir;
    WDecor decor;
    int nchildren;
    struct WLayoutNode **child; /* length nchildren when dir != WL_SINGLE */
    int *weight;                /* per-child weight for size distribution */
    int leaf_index;             /* index into E.windows when dir==WL_SINGLE and kind==window; else -1 */
    int leaf_kind;              /* 0=window, 1=quickfix (for WL_SINGLE) */
    int fixed_size;             /* if >0, fixed size along split axis (rows for H, cols for V) */
    /* Cached computed rectangle (outer, including borders) */
    int top, left, height, width;
} WLayoutNode;

/* Initialization and teardown */
WLayoutNode *wlayout_init_root(int leaf_index);
void wlayout_free(WLayoutNode *node);

/* Mutations */
/* Transform a leaf into a split with the existing leaf and a new leaf index. */
void wlayout_split_leaf(WLayoutNode *leaf, WSplitDir dir, int new_leaf_index);
/* Close a leaf by index; returns new root in case the root was removed/collapsed. */
WLayoutNode *wlayout_close_leaf(WLayoutNode *root, int leaf_index);
/* After removing a window at closed_idx and shifting the E.windows array left,
 * update remaining leaf indices (> closed_idx). */
void wlayout_reindex_after_close(WLayoutNode *node, int closed_idx);
/* Find the leaf node by window index */
WLayoutNode *wlayout_find_leaf_by_index(WLayoutNode *node, int leaf_index);

/* Layout computation and rendering */
void wlayout_compute(WLayoutNode *root, int top, int left, int height, int width);
void wlayout_draw_decorations(Abuf *ab, const WLayoutNode *root);

/* Utilities for global decoration control */
void wlayout_set_enabled_all(WLayoutNode *root, int enabled);
void wlayout_set_thickness_all(WLayoutNode *root, int thickness);

/* Adjust weight of the split containing the given leaf index by delta. */
int wlayout_adjust_weight(WLayoutNode *root, int leaf_index, int delta);

/* Ensure quickfix leaf presence in root tree according to open/height. */
void wlayout_sync_quickfix(WLayoutNode **root, int qf_open, int qf_height);

/* Ensure terminal pane leaf presence in root tree according to open/height. */
void wlayout_sync_term(WLayoutNode **root, int term_open, int term_height, int term_idx);



#endif /* WLAYOUT_H */
