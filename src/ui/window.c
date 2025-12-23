#include "editor.h"
#include "safe_string.h"
#include "wlayout.h"
#include "winmodal.h"

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
    E.windows.data[0].is_modal = 0;
    E.windows.data[0].visible = 1;
    E.windows.data[0].row_offset = 0;
    E.windows.data[0].col_offset = 0;
    E.windows.data[0].wrap = E.default_wrap;
    E.windows.data[0].cursor.x = 0;
    E.windows.data[0].cursor.y = 0;
    E.windows.data[0].gutter_mode = 0;
    E.windows.data[0].gutter_fixed_width = 0;
    E.windows.data[0].sel.type = SEL_NONE;
}

/* Update geometry for the single main window. (unused) */

Window *window_cur(void) {
    /* If a modal is shown, it takes priority */
    if (E.modal_window && E.modal_window->visible)
        return E.modal_window;

    if (E.windows.len == 0)
        return NULL;
    return &E.windows.data[E.current_window];
}

void win_attach_buf(Window *win, Buffer *buf) {
    if (!PTR_VALID(win) || !PTR_VALID(buf))
        return;
    /* Validate buf is actually from E.buffers array */
    if (buf < E.buffers.data || buf >= E.buffers.data + E.buffers.len)
        return;
    int idx = (int)(buf - E.buffers.data);
    if (!BOUNDS_CHECK(idx, E.buffers.len))
        return; /* Extra safety check */
    win->buffer_index = idx;
    win->cursor = buf->cursor;
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
    if (!PTR_VALID(cur))
        return;
    int prev_idx = E.current_window;
    int new_idx = E.windows.len;
    E.windows.data[new_idx] = *cur; /* copy state */
    E.windows.data[new_idx].focus = 1;
    E.windows.data[new_idx].sel.type = SEL_NONE;
    cur->focus = 0;
    E.windows.len++;
    E.current_window = new_idx;
    E.current_buffer = E.windows.data[new_idx].buffer_index;
    if (!E.wlayout_root) {
        /* Fallback: create a root with one leaf and split */
        E.wlayout_root = wlayout_init_root(0);
    }
    WLayoutNode *leaf = wlayout_find_leaf_by_index(
        E.wlayout_root, new_idx == 0 ? 0 : E.current_window - 1);
    if (!leaf) {
        /* Try current index */
        leaf = wlayout_find_leaf_by_index(E.wlayout_root, E.current_window);
    }
    if (!leaf) {
        /* As a fallback, split root if single */
        leaf = E.wlayout_root;
    }
    /* Split the leaf containing the previously focused window; place new window
     * to the right */
    WLayoutNode *base_leaf =
        wlayout_find_leaf_by_index(E.wlayout_root, prev_idx);
    if (!base_leaf)
        base_leaf = leaf;
    wlayout_split_leaf(base_leaf, WL_VERTICAL, new_idx);
}

void windows_split_horizontal(void) {
    /* Ensure capacity for new window */
    if (!vec_reserve_typed(&E.windows, E.windows.len + 1, sizeof(Window))) {
        ed_set_status_message("out of memory");
        return;
    }
    Window *cur = window_cur();
    if (!PTR_VALID(cur))
        return;
    int prev_idx = E.current_window;
    int new_idx = E.windows.len;
    E.windows.data[new_idx] = *cur; /* copy state */
    E.windows.data[new_idx].focus = 1;
    E.windows.data[new_idx].sel.type = SEL_NONE;
    cur->focus = 0;
    E.windows.len++;
    E.current_window = new_idx;
    E.current_buffer = E.windows.data[new_idx].buffer_index;
    if (!E.wlayout_root) {
        E.wlayout_root = wlayout_init_root(0);
    }
    WLayoutNode *base_leaf =
        wlayout_find_leaf_by_index(E.wlayout_root, prev_idx);
    if (!base_leaf)
        base_leaf = E.wlayout_root;
    wlayout_split_leaf(base_leaf, WL_HORIZONTAL, new_idx);
}

void windows_focus_next(void) {
    if (E.windows.len <= 1)
        return;
    E.windows.data[E.current_window].focus = 0;
    E.current_window = (E.current_window + 1) % E.windows.len;
    E.windows.data[E.current_window].focus = 1;
    /* Sync current buffer with focused window */
    E.current_buffer = E.windows.data[E.current_window].buffer_index;
}

static void windows_focus_set(int idx) {
    if (!BOUNDS_CHECK(idx, E.windows.len))
        return;
    for (int i = 0; i < (int)E.windows.len; i++) {
        E.windows.data[i].focus = 0;
    }
    E.current_window = idx;
    E.windows.data[idx].focus = 1;
    E.current_buffer = E.windows.data[idx].buffer_index;
}

/* Find neighbor window index in a given direction relative to current.
 * dir: 0=left, 1=right, 2=up, 3=down. Returns -1 if none. */
static int windows_find_neighbor(int dir) {
    if (E.windows.len <= 1)
        return -1;
    if (!BOUNDS_CHECK(E.current_window, E.windows.len))
        return -1;
    Window *cur = &E.windows.data[E.current_window];

    int cur_top = cur->top;
    int cur_left = cur->left;
    int cur_bottom = cur->top + cur->height - 1;
    int cur_right = cur->left + cur->width - 1;

    int best = -1;
    int best_metric = 0;

    for (int i = 0; i < (int)E.windows.len; i++) {
        if (i == E.current_window)
            continue;
        Window *w = &E.windows.data[i];
        int w_top = w->top;
        int w_left = w->left;
        int w_bottom = w->top + w->height - 1;
        int w_right = w->left + w->width - 1;

        if (dir == 0) { /* left */
            /* Must be strictly left and vertically overlapping */
            if (w_right >= cur_left)
                continue;
            if (w_bottom < cur_top || w_top > cur_bottom)
                continue;
            int dist = cur_left - w_right;
            if (best < 0 || dist < best_metric) {
                best = i;
                best_metric = dist;
            }
        } else if (dir == 1) { /* right */
            if (w_left <= cur_right)
                continue;
            if (w_bottom < cur_top || w_top > cur_bottom)
                continue;
            int dist = w_left - cur_right;
            if (best < 0 || dist < best_metric) {
                best = i;
                best_metric = dist;
            }
        } else if (dir == 2) { /* up */
            if (w_bottom >= cur_top)
                continue;
            if (w_right < cur_left || w_left > cur_right)
                continue;
            int dist = cur_top - w_bottom;
            if (best < 0 || dist < best_metric) {
                best = i;
                best_metric = dist;
            }
        } else if (dir == 3) { /* down */
            if (w_top <= cur_bottom)
                continue;
            if (w_right < cur_left || w_left > cur_right)
                continue;
            int dist = w_top - cur_bottom;
            if (best < 0 || dist < best_metric) {
                best = i;
                best_metric = dist;
            }
        }
    }

    return best;
}

void windows_focus_left(void) {
    int idx = windows_find_neighbor(0);
    if (idx >= 0) {
        windows_focus_set(idx);
    } else {
        ed_set_status_message("no window left");
    }
}

void windows_focus_right(void) {
    int idx = windows_find_neighbor(1);
    if (idx >= 0) {
        windows_focus_set(idx);
    } else {
        ed_set_status_message("no window right");
    }
}

void windows_focus_up(void) {
    int idx = windows_find_neighbor(2);
    if (idx >= 0) {
        windows_focus_set(idx);
    } else {
        ed_set_status_message("no window up");
    }
}

void windows_focus_down(void) {
    int idx = windows_find_neighbor(3);
    if (idx >= 0) {
        windows_focus_set(idx);
    } else {
        ed_set_status_message("no window down");
    }
}

void windows_close_current(void) {
	if (winmodal_is_shown()){
		 winmodal_destroy(winmodal_current());
		return;
	}
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
    if (E.wlayout_root)
        wlayout_reindex_after_close(E.wlayout_root, idx);
    if (E.current_window >= (int)E.windows.len)
        E.current_window = E.windows.len - 1;
    /* Focus the resulting window */
    for (int i = 0; i < (int)E.windows.len; i++)
        E.windows.data[i].focus = 0;
    if (E.current_window >= 0) {
        E.windows.data[E.current_window].focus = 1;
        E.current_buffer = E.windows.data[E.current_window].buffer_index;
    }
    ed_set_status_message("closed window (%d remaining)", E.windows.len);
}
