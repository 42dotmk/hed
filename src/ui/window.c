#include "hed.h"

/* Initialize window system with a single full-screen window. */
void windows_init(void) {
    E.windows.len = 1;
    E.current_window = 0;
    E.window_layout = 0;
    E.windows.data[0].top = 1;
    E.windows.data[0].left = 1;
    E.windows.data[0].height = E.screen_rows; /* content rows (set in ed_init) */
    E.windows.data[0].width = E.screen_cols;
    E.windows.data[0].buffer_index = E.current_buffer;
    E.windows.data[0].focus = 1;
    E.windows.data[0].is_quickfix = 0;
    E.windows.data[0].row_offset = 0;
    E.windows.data[0].col_offset = 0;
    E.windows.data[0].wrap = E.default_wrap;
    E.windows.data[0].cursor.x = 0;
    E.windows.data[0].cursor.y = 0;
    E.windows.data[0].gutter_mode = 0;
    E.windows.data[0].gutter_fixed_width = 0;
}

/* Update geometry for the single main window. (unused) */

Window *window_cur(void) {
    if (E.windows.len == 0) return NULL;
    return &E.windows.data[E.current_window];
}

void win_attach_buf(Window *win, Buffer *buf) {
    if (!PTR_VALID(win) || !PTR_VALID(buf)) return;
    /* Validate buf is actually from E.buffers array */
    if (buf < E.buffers.data || buf >= E.buffers.data + E.buffers.len) return;
    int idx = (int)(buf - E.buffers.data);
    if (!BOUNDS_CHECK(idx, E.buffers.len)) return;  /* Extra safety check */
    win->buffer_index = idx;
    if (win->focus) {
        E.current_buffer = idx;
    }
}

void windows_split_vertical(void) {
    /* Ensure capacity for new window */
    if (!vec_reserve_typed(&E.windows, E.windows.len + 1, sizeof(Window))) {
        ed_set_status_message("out of memory");
        return;
    }
    Window *cur = window_cur();
    if (!PTR_VALID(cur)) return;
    int prev_idx = E.current_window;
    int new_idx = E.windows.len;
    E.windows.data[new_idx] = *cur; /* copy state */
    E.windows.data[new_idx].focus = 1;
    cur->focus = 0;
    E.windows.len++;
    E.current_window = new_idx;
    E.current_buffer = E.windows.data[new_idx].buffer_index;
    if (!E.wlayout_root) {
        /* Fallback: create a root with one leaf and split */
        E.wlayout_root = wlayout_init_root(0);
    }
    WLayoutNode *leaf = wlayout_find_leaf_by_index(E.wlayout_root, new_idx == 0 ? 0 : E.current_window - 1);
    if (!leaf) {
        /* Try current index */
        leaf = wlayout_find_leaf_by_index(E.wlayout_root, E.current_window);
    }
    if (!leaf) {
        /* As a fallback, split root if single */
        leaf = E.wlayout_root;
    }
    /* Split the leaf containing the previously focused window; place new window to the right */
    WLayoutNode *base_leaf = wlayout_find_leaf_by_index(E.wlayout_root, prev_idx);
    if (!base_leaf) base_leaf = leaf;
    wlayout_split_leaf(base_leaf, WL_VERTICAL, new_idx);
}

void windows_split_horizontal(void) {
    /* Ensure capacity for new window */
    if (!vec_reserve_typed(&E.windows, E.windows.len + 1, sizeof(Window))) {
        ed_set_status_message("out of memory");
        return;
    }
    Window *cur = window_cur();
    if (!PTR_VALID(cur)) return;
    int prev_idx = E.current_window;
    int new_idx = E.windows.len;
    E.windows.data[new_idx] = *cur; /* copy state */
    E.windows.data[new_idx].focus = 1;
    cur->focus = 0;
    E.windows.len++;
    E.current_window = new_idx;
    E.current_buffer = E.windows.data[new_idx].buffer_index;
    if (!E.wlayout_root) {
        E.wlayout_root = wlayout_init_root(0);
    }
    WLayoutNode *base_leaf = wlayout_find_leaf_by_index(E.wlayout_root, prev_idx);
    if (!base_leaf) base_leaf = E.wlayout_root;
    wlayout_split_leaf(base_leaf, WL_HORIZONTAL, new_idx);
}

void windows_focus_next(void) {
    if (E.windows.len <= 1) return;
    E.windows.data[E.current_window].focus = 0;
    E.current_window = (E.current_window + 1) % E.windows.len;
    E.windows.data[E.current_window].focus = 1;
    /* Sync current buffer with focused window */
    E.current_buffer = E.windows.data[E.current_window].buffer_index;
}

/* windows_on_resize was unused and removed. */

void windows_close_current(void) {
    if (E.windows.len <= 1) {
        ed_set_status_message("only one window");
        return;
    }
    int idx = E.current_window;
    /* Update layout tree first */
    if (E.wlayout_root) {
        E.wlayout_root = wlayout_close_leaf(E.wlayout_root, idx);
    }
    /* Shift windows array down and update indices in layout tree */
    for (int i = idx; i < (int)E.windows.len - 1; i++) {
        E.windows.data[i] = E.windows.data[i + 1];
    }
    E.windows.len--;
    if (E.wlayout_root) wlayout_reindex_after_close(E.wlayout_root, idx);
    if (E.current_window >= (int)E.windows.len) E.current_window = E.windows.len - 1;
    /* Focus the resulting window */
    for (int i = 0; i < (int)E.windows.len; i++) E.windows.data[i].focus = 0;
    if (E.current_window >= 0) {
        E.windows.data[E.current_window].focus = 1;
        E.current_buffer = E.windows.data[E.current_window].buffer_index;
    }
    ed_set_status_message("closed window (%d remaining)", E.windows.len);
}
