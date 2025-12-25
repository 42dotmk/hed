#include "ansi.h"
#include "hed.h"

static void wdecor_set_defaults(WDecor *d, int for_split_node) {
    if (!d)
        return;
    d->enabled = for_split_node ? 1 : 0;
    d->thickness = 1;
    d->top = 0;
    d->bottom = 0;
    d->left = 0;
    d->right = 0;
    /* UTF-8 box drawing characters */
    d->chars.v = "│";
    d->chars.h = "─";
    d->chars.tl = "┌";
    d->chars.tr = "┐";
    d->chars.bl = "└";
    d->chars.br = "┘";
    d->chars.tee_t = "┬";
    d->chars.tee_b = "┴";
    d->chars.tee_l = "├";
    d->chars.tee_r = "┤";
    d->chars.cross = "┼";
}

static WLayoutNode *wlayout_node_new_leaf(int leaf_index) {
    WLayoutNode *n = calloc(1, sizeof(WLayoutNode));
    if (!n)
        return NULL; /* Return NULL on OOM */
    n->parent = NULL;
    n->dir = WL_SINGLE;
    n->leaf_index = leaf_index;
    n->leaf_kind = 0;
    n->fixed_size = 0;
    n->nchildren = 0;
    n->child = NULL;
    n->weight = NULL;
    wdecor_set_defaults(&n->decor, 0);
    return n;
}

/* (unused helper previously used during development) */
/* static WLayoutNode *wlayout_node_new_split(WSplitDir dir, int nchildren); */

WLayoutNode *wlayout_init_root(int leaf_index) {
    return wlayout_node_new_leaf(leaf_index);
}

static void wlayout_free_rec(WLayoutNode *n) {
    if (!n)
        return;
    if (n->dir != WL_SINGLE) {
        for (int i = 0; i < n->nchildren; i++)
            wlayout_free_rec(n->child[i]);
        free(n->child);
        free(n->weight);
    }
    free(n);
}

void wlayout_free(WLayoutNode *node) { wlayout_free_rec(node); }

WLayoutNode *wlayout_find_leaf_by_index(WLayoutNode *node, int leaf_index) {
    if (!node)
        return NULL;
    if (node->dir == WL_SINGLE)
        return node->leaf_index == leaf_index ? node : NULL;
    for (int i = 0; i < node->nchildren; i++) {
        WLayoutNode *f = wlayout_find_leaf_by_index(node->child[i], leaf_index);
        if (f)
            return f;
    }
    return NULL;
}

void wlayout_split_leaf(WLayoutNode *leaf, WSplitDir dir, int new_leaf_index) {
    if (!leaf || leaf->dir != WL_SINGLE)
        return;
    /* Convert this leaf node into a split node with two children: old + new */
    int old_idx = leaf->leaf_index;
    /* Prepare child nodes */
    WLayoutNode *c0 = wlayout_node_new_leaf(old_idx);
    WLayoutNode *c1 = wlayout_node_new_leaf(new_leaf_index);
    if (!c0 || !c1) {
        free(c0);
        free(c1); /* Clean up on OOM */
        return;
    }
    /* Rebuild current node as split */
    leaf->dir = dir;
    leaf->leaf_index = -1;
    leaf->nchildren = 2;
    leaf->child = calloc(2, sizeof(WLayoutNode *));
    leaf->weight = calloc(2, sizeof(int));
    if (!leaf->child || !leaf->weight) {
        free(leaf->child);
        free(leaf->weight);
        free(c0);
        free(c1);
        leaf->dir = WL_SINGLE; /* Restore state on OOM */
        leaf->leaf_index = old_idx;
        leaf->nchildren = 0;
        return;
    }
    leaf->child[0] = c0;
    c0->parent = leaf;
    leaf->weight[0] = 1;
    leaf->child[1] = c1;
    c1->parent = leaf;
    leaf->weight[1] = 1;
    wdecor_set_defaults(&leaf->decor, 1);
}

static void wlayout_collapse_if_needed(WLayoutNode *node) {
    if (!node)
        return;
    if (node->dir == WL_SINGLE)
        return;
    /* Collapse nodes with single child: hoist child into this node */
    if (node->nchildren == 1) {
        WLayoutNode *c = node->child[0];
        /* Copy child's content into node */
        WSplitDir dir = c->dir;
        int leaf_index = c->leaf_index;
        int nchildren = c->nchildren;
        WLayoutNode **child = c->child;
        int *weight = c->weight;
        WDecor decor = c->decor;

        /* Free current arrays */
        free(node->child);
        node->child = NULL;
        free(node->weight);
        node->weight = NULL;

        node->dir = dir;
        node->leaf_index = leaf_index;
        node->nchildren = nchildren;
        node->child = child;
        node->weight = weight;
        node->decor = decor;
        for (int i = 0; i < node->nchildren; i++)
            if (node->child[i])
                node->child[i]->parent = node;
        /* Free the hoisted node wrapper */
        free(c);
    }
}

WLayoutNode *wlayout_close_leaf(WLayoutNode *root, int leaf_index) {
    if (!root)
        return NULL;
    WLayoutNode *leaf = wlayout_find_leaf_by_index(root, leaf_index);
    if (!leaf)
        return root;
    if (leaf == root) {
        /* Root leaf: keep it as an empty placeholder? For now, don't remove;
         * caller must keep at least one window. */
        return root;
    }
    WLayoutNode *p = leaf->parent;
    if (!p)
        return root;
    /* Remove leaf from parent */
    if (p->nchildren == 2) {
        /* Replace parent with the sibling (collapse) */
        WLayoutNode *sibling =
            (p->child[0] == leaf) ? p->child[1] : p->child[0];
        WLayoutNode *gp = p->parent;
        /* Detach to avoid double free */
        free(leaf); /* remove closed leaf node */
        /* Now hoist sibling over p */
        if (!gp) {
            /* p is root, sibling becomes new root */
            sibling->parent = NULL;
            free(p->child);
            free(p->weight);
            free(p);
            root = sibling;
        } else {
            /* Replace p in gp's children with sibling */
            for (int i = 0; i < gp->nchildren; i++) {
                if (gp->child[i] == p) {
                    gp->child[i] = sibling;
                    sibling->parent = gp;
                    break;
                }
            }
            free(p->child);
            free(p->weight);
            free(p);
            /* Might allow further collapse up the tree */
            wlayout_collapse_if_needed(gp);
        }
    } else if (p->nchildren > 2) {
        /* Remove leaf from the array and shift left */
        int pos = -1;
        for (int i = 0; i < p->nchildren; i++)
            if (p->child[i] == leaf) {
                pos = i;
                break;
            }
        if (pos >= 0) {
            free(leaf);
            for (int i = pos; i < p->nchildren - 1; i++)
                p->child[i] = p->child[i + 1], p->weight[i] = p->weight[i + 1];
            p->nchildren--;
        }
        wlayout_collapse_if_needed(p);
    }
    return root;
}

void wlayout_reindex_after_close(WLayoutNode *node, int closed_idx) {
    if (!node)
        return;
    if (node->dir == WL_SINGLE) {
        if (node->leaf_index > closed_idx)
            node->leaf_index -= 1;
        return;
    }
    for (int i = 0; i < node->nchildren; i++)
        wlayout_reindex_after_close(node->child[i], closed_idx);
}

static void compute_node(WLayoutNode *node, int top, int left, int height,
                         int width) {
    if (!node)
        return;
    node->top = top;
    node->left = left;
    node->height = height;
    node->width = width;
    int t =
        (node->decor.enabled && node->decor.top) ? node->decor.thickness : 0;
    int b =
        (node->decor.enabled && node->decor.bottom) ? node->decor.thickness : 0;
    int l =
        (node->decor.enabled && node->decor.left) ? node->decor.thickness : 0;
    int r =
        (node->decor.enabled && node->decor.right) ? node->decor.thickness : 0;
    if (t < 0)
        t = 0;
    if (b < 0)
        b = 0;
    if (l < 0)
        l = 0;
    if (r < 0)
        r = 0;
    int itop = top + t;
    int ileft = left + l;
    int iheight = height - (t + b);
    int iwidth = width - (l + r);
    if (iheight < 1)
        iheight = 1;
    if (iwidth < 1)
        iwidth = 1;

    if (node->dir == WL_SINGLE) {
        /* Assign geometry to the target leaf */
        if (node->leaf_index >= 0 && node->leaf_index < (int)E.windows.len) {
            Window *w = &E.windows.data[node->leaf_index];
            w->top = itop;
            w->left = ileft;
            w->height = iheight;
            w->width = iwidth;
        }
        return;
    }
    int sep = node->decor.thickness > 0 ? node->decor.thickness : 1;
    int sumw = 0;
    for (int i = 0; i < node->nchildren; i++)
        sumw += node->weight[i] > 0 ? node->weight[i] : 1;
    if (sumw <= 0)
        sumw = node->nchildren;

    if (node->dir == WL_VERTICAL) {
        int gaps = (node->nchildren - 1) * sep;
        int fixed = 0;
        for (int i = 0; i < node->nchildren; i++)
            fixed += (node->child[i] && node->child[i]->fixed_size > 0)
                         ? node->child[i]->fixed_size
                         : 0;
        int avail = iwidth - gaps - fixed;
        if (avail < 0)
            avail = 0;
        int used = 0;
        int cur_left = ileft;
        for (int i = 0; i < node->nchildren; i++) {
            int w;
            if (node->child[i] && node->child[i]->fixed_size > 0)
                w = node->child[i]->fixed_size;
            else
                w = (i == node->nchildren - 1)
                        ? (avail - used)
                        : (avail * (node->weight[i] > 0 ? node->weight[i] : 1) /
                           sumw);
            if (w < 1)
                w = 1;
            compute_node(node->child[i], itop, cur_left, iheight, w);
            cur_left += w;
            if (i != node->nchildren - 1)
                cur_left += sep; /* leave space for separator */
            used += w;
        }
    } else if (node->dir == WL_HORIZONTAL) {
        int gaps = (node->nchildren - 1) * sep;
        int fixed = 0;
        for (int i = 0; i < node->nchildren; i++)
            fixed += (node->child[i] && node->child[i]->fixed_size > 0)
                         ? node->child[i]->fixed_size
                         : 0;
        int avail = iheight - gaps - fixed;
        if (avail < 0)
            avail = 0;
        int used = 0;
        int cur_top = itop;
        for (int i = 0; i < node->nchildren; i++) {
            int h;
            if (node->child[i] && node->child[i]->fixed_size > 0)
                h = node->child[i]->fixed_size;
            else
                h = (i == node->nchildren - 1)
                        ? (avail - used)
                        : (avail * (node->weight[i] > 0 ? node->weight[i] : 1) /
                           sumw);
            if (h < 1)
                h = 1;
            compute_node(node->child[i], cur_top, ileft, h, iwidth);
            cur_top += h;
            if (i != node->nchildren - 1)
                cur_top += sep; /* leave space for separator */
            used += h;
        }
    }
}

void wlayout_compute(WLayoutNode *root, int top, int left, int height,
                     int width) {
    compute_node(root, top, left, height, width);
}

static void draw_rect_border(Abuf *ab, const WLayoutNode *n) {
    if (!n || !n->decor.enabled)
        return;
    int t = n->decor.top ? n->decor.thickness : 0;
    int b = n->decor.bottom ? n->decor.thickness : 0;
    int l = n->decor.left ? n->decor.thickness : 0;
    int r = n->decor.right ? n->decor.thickness : 0;
    if (t == 0 && b == 0 && l == 0 && r == 0)
        return;
    int top = n->top, left = n->left, h = n->height, w = n->width;
    if (t > 0) {
        for (int row = 0; row < t; row++) {
            ansi_move(ab, top + row, left);
            for (int i = 0; i < w; i++)
                ab_append_str(ab, n->decor.chars.h);
        }
    }
    if (b > 0) {
        for (int row = 0; row < b; row++) {
            ansi_move(ab, top + h - 1 - row, left);
            for (int i = 0; i < w; i++)
                ab_append_str(ab, n->decor.chars.h);
        }
    }
    if (l > 0) {
        for (int col = 0; col < l; col++) {
            for (int y = 0; y < h; y++) {
                ansi_move(ab, top + y, left + col);
                ab_append_str(ab, n->decor.chars.v);
            }
        }
    }
    if (r > 0) {
        for (int col = 0; col < r; col++) {
            for (int y = 0; y < h; y++) {
                ansi_move(ab, top + y, left + w - 1 - col);
                ab_append_str(ab, n->decor.chars.v);
            }
        }
    }
}

static void draw_node(Abuf *ab, const WLayoutNode *n) {
    if (!n)
        return;
    draw_rect_border(ab, n);
    if (n->dir == WL_SINGLE)
        return;
    if (n->decor.enabled && n->dir == WL_VERTICAL) {
        for (int i = 0; i < n->nchildren - 1; i++) {
            const WLayoutNode *a = n->child[i];
            const WLayoutNode *b = n->child[i + 1];
            int x0 = a->left + a->width;
            int x1 = b->left - 1;
            for (int x = x0; x <= x1; x++) {
                for (int y = n->top; y < n->top + n->height; y++) {
                    ansi_move(ab, y, x);
                    ab_append_str(ab, n->decor.chars.v);
                }
            }
        }
    } else if (n->decor.enabled && n->dir == WL_HORIZONTAL) {
        for (int i = 0; i < n->nchildren - 1; i++) {
            const WLayoutNode *a = n->child[i];
            const WLayoutNode *b = n->child[i + 1];
            int y0 = a->top + a->height;
            int y1 = b->top - 1;
            for (int y = y0; y <= y1; y++) {
                ansi_move(ab, y, n->left);
                for (int x = 0; x < n->width; x++)
                    ab_append_str(ab, n->decor.chars.h);
            }
        }
    }
    for (int i = 0; i < n->nchildren; i++)
        draw_node(ab, n->child[i]);
}

void wlayout_draw_decorations(Abuf *ab, const WLayoutNode *root) {
    draw_node(ab, root);
}

static void set_enabled_all(WLayoutNode *n, int en) {
    if (!n)
        return;
    n->decor.enabled = en ? 1 : 0;
    if (n->dir != WL_SINGLE) {
        for (int i = 0; i < n->nchildren; i++)
            set_enabled_all(n->child[i], en);
    }
}

void wlayout_set_enabled_all(WLayoutNode *root, int enabled) {
    set_enabled_all(root, enabled);
}

static void set_thickness_all(WLayoutNode *n, int th) {
    if (!n)
        return;
    if (th < 1)
        th = 1;
    if (th > 8)
        th = 8;
    n->decor.thickness = th;
    if (n->dir != WL_SINGLE) {
        for (int i = 0; i < n->nchildren; i++)
            set_thickness_all(n->child[i], th);
    }
}

void wlayout_set_thickness_all(WLayoutNode *root, int thickness) {
    set_thickness_all(root, thickness);
}

static int find_parent_and_pos(WLayoutNode *n, int leaf_index,
                               WLayoutNode **out_parent, int *out_pos) {
    if (!n || n->dir == WL_SINGLE)
        return 0;
    for (int i = 0; i < n->nchildren; i++) {
        WLayoutNode *c = n->child[i];
        if (!c)
            continue;
        if (c->dir == WL_SINGLE && c->leaf_index == leaf_index) {
            if (out_parent)
                *out_parent = n;
            if (out_pos)
                *out_pos = i;
            return 1;
        }
        if (c->dir != WL_SINGLE) {
            if (find_parent_and_pos(c, leaf_index, out_parent, out_pos))
                return 1;
        }
    }
    return 0;
}

int wlayout_adjust_weight(WLayoutNode *root, int leaf_index, int delta) {
    if (!root || root->dir == WL_SINGLE)
        return 0;
    WLayoutNode *parent = NULL;
    int pos = -1;
    if (!find_parent_and_pos(root, leaf_index, &parent, &pos))
        return 0;
    if (!parent || pos < 0 || pos >= parent->nchildren)
        return 0;
    int w = parent->weight[pos];
    if (w <= 0)
        w = 1;
    w += delta;
    if (w < 1)
        w = 1;
    if (w > 1000)
        w = 1000;
    parent->weight[pos] = w;
    return 1;
}
