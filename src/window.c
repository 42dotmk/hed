#include "hed.h"

/* Initialize window system with a single full-screen window. */
void windows_init(void) {
    E.num_windows = 1;
    E.current_window = 0;
    E.window_layout = 0;
    E.windows[0].top = 1;
    E.windows[0].left = 1;
    E.windows[0].height = E.screen_rows; /* content rows (set in ed_init) */
    E.windows[0].width = E.screen_cols;
    E.windows[0].buffer_index = E.current_buffer;
    E.windows[0].focus = 1;
    E.windows[0].row_offset = 0;
    E.windows[0].col_offset = 0;
    E.windows[0].cursor_x = 0;
    E.windows[0].cursor_y = 0;
    E.windows[0].visual_start_x = 0;
    E.windows[0].visual_start_y = 0;
}

/* Update geometry for the single main window. */
void windows_set_geometry(int top, int left, int height, int width) {
    if (E.num_windows <= 0) return;
    if (height < 1) height = 1;
    if (width < 1) width = 1;
    E.windows[0].top = top;
    E.windows[0].left = left;
    E.windows[0].height = height;
    E.windows[0].width = width;
}

Window *window_cur(void) {
    if (E.num_windows == 0) return NULL;
    return &E.windows[E.current_window];
}

void windows_split_vertical(void) {
    if (E.num_windows >= 2) { ed_set_status_message("max windows"); return; }
    Window *w0 = &E.windows[0];
    int left_w = w0->width / 2;
    if (left_w < 10) left_w = 10;
    int right_w = w0->width - left_w;
    E.windows[0].width = left_w;
    E.windows[1] = *w0; /* copy base settings */
    E.windows[1].left = w0->left + left_w;
    E.windows[1].width = right_w;
    E.windows[1].focus = 1;
    E.windows[1].row_offset = w0->row_offset;
    E.windows[1].col_offset = w0->col_offset;
    E.windows[1].buffer_index = w0->buffer_index;
    E.windows[1].visual_start_x = w0->visual_start_x;
    E.windows[1].visual_start_y = w0->visual_start_y;
    E.windows[0].focus = 0;
    E.num_windows = 2;
    E.current_window = 1;
    E.window_layout = 1;
    E.current_buffer = E.windows[E.current_window].buffer_index;
}

void windows_split_horizontal(void) {
    if (E.num_windows >= 2) { ed_set_status_message("max windows"); return; }
    Window *w0 = &E.windows[0];
    int top_h = w0->height / 2;
    if (top_h < 3) top_h = 3;
    int bottom_h = w0->height - top_h;
    E.windows[0].height = top_h;
    E.windows[1] = *w0; /* copy base settings */
    E.windows[1].top = w0->top + top_h;
    E.windows[1].height = bottom_h;
    E.windows[1].focus = 1;
    E.windows[1].row_offset = w0->row_offset;
    E.windows[1].col_offset = w0->col_offset;
    E.windows[1].buffer_index = w0->buffer_index;
    E.windows[1].visual_start_x = w0->visual_start_x;
    E.windows[1].visual_start_y = w0->visual_start_y;
    E.windows[0].focus = 0;
    E.num_windows = 2;
    E.current_window = 1;
    E.window_layout = 2;
    E.current_buffer = E.windows[E.current_window].buffer_index;
}

void windows_focus_next(void) {
    if (E.num_windows <= 1) return;
    E.windows[E.current_window].focus = 0;
    E.current_window = (E.current_window + 1) % E.num_windows;
    E.windows[E.current_window].focus = 1;
    /* Sync current buffer with focused window */
    E.current_buffer = E.windows[E.current_window].buffer_index;
}

void windows_on_resize(int content_rows, int cols) {
    if (E.num_windows <= 0) return;
    if (E.window_layout == 0) {
        E.windows[0].top = 1;
        E.windows[0].left = 1;
        E.windows[0].height = content_rows;
        E.windows[0].width = cols;
    } else if (E.window_layout == 1 && E.num_windows == 2) {
        int left_w = cols / 2; if (left_w < 10) left_w = 10;
        int right_w = cols - left_w;
        E.windows[0].top = 1;
        E.windows[0].left = 1;
        E.windows[0].height = content_rows;
        E.windows[0].width = left_w;
        E.windows[1].top = 1;
        E.windows[1].left = 1 + left_w;
        E.windows[1].height = content_rows;
        E.windows[1].width = right_w;
    } else if (E.window_layout == 2 && E.num_windows == 2) {
        int top_h = content_rows / 2; if (top_h < 3) top_h = 3;
        int bottom_h = content_rows - top_h;
        E.windows[0].top = 1;
        E.windows[0].left = 1;
        E.windows[0].height = top_h;
        E.windows[0].width = cols;
        E.windows[1].top = 1 + top_h;
        E.windows[1].left = 1;
        E.windows[1].height = bottom_h;
        E.windows[1].width = cols;
    }
}

void windows_close_current(void) {
    if (E.num_windows <= 1) {
        ed_set_status_message("only one window");
        return;
    }
    int idx = E.current_window;
    /* Shift windows down */
    for (int i = idx; i < E.num_windows - 1; i++) {
        E.windows[i] = E.windows[i + 1];
    }
    E.num_windows--;
    if (E.current_window >= E.num_windows) E.current_window = E.num_windows - 1;
    /* Focus the resulting window */
    for (int i = 0; i < E.num_windows; i++) E.windows[i].focus = 0;
    E.windows[E.current_window].focus = 1;
    /* Sync buffer index to editor current_buffer */
    if (E.current_window >= 0) E.current_buffer = E.windows[E.current_window].buffer_index;
    ed_set_status_message("closed window (%d remaining)", E.num_windows);
}
