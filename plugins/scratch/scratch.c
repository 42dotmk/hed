/* scratch plugin: an ephemeral, unnamed buffer reachable in one keystroke.
 *
 * `:scratch` opens (or focuses) a vertical split showing the shared
 * "[scratch]" buffer. Contents persist for the lifetime of the editor
 * session. The buffer has no filename and is kept non-dirty so `:q`
 * closes the split without complaint. */

#include "hed.h"

#define SCRATCH_TITLE "[scratch]"

static int scratch_find_buf(void) {
    for (int i = 0; i < (int)arrlen(E.buffers); i++) {
        const char *t = E.buffers[i].title;
        if (t && strcmp(t, SCRATCH_TITLE) == 0)
            return i;
    }
    return -1;
}

static int scratch_find_or_create_buf(void) {
    int idx = scratch_find_buf();
    if (idx >= 0) return idx;

    int new_idx = -1;
    if (buf_new(NULL, &new_idx) != ED_OK) return -1;

    Buffer *b = &E.buffers[new_idx];
    free(b->filename); b->filename = NULL;
    free(b->title);    b->title    = strdup(SCRATCH_TITLE);
    b->dirty = 0;
    return new_idx;
}

static int scratch_find_window(int buf_idx) {
    for (int i = 0; i < (int)arrlen(E.windows); i++) {
        Window *w = &E.windows[i];
        if (w->visible && !w->is_modal && w->buffer_index == buf_idx)
            return i;
    }
    return -1;
}

static void cmd_scratch(const char *args) {
    (void)args;

    int buf_idx = scratch_find_or_create_buf();
    if (buf_idx < 0) {
        ed_set_status_message("scratch: failed to create buffer");
        return;
    }

    int win_idx = scratch_find_window(buf_idx);
    if (win_idx >= 0) {
        if (win_idx != E.current_window) {
            E.windows[E.current_window].focus = 0;
            E.windows[win_idx].focus = 1;
            E.current_window = win_idx;
            E.current_buffer = buf_idx;
        }
        return;
    }

    windows_split_vertical();
    Window *w = window_cur();
    if (w) win_attach_buf(w, &E.buffers[buf_idx]);
    E.buffers[buf_idx].dirty = 0;
}

static int scratch_init(void) {
    cmd("scratch", cmd_scratch, "open/focus the scratch buffer in a vsplit");
    return 0;
}

const Plugin plugin_scratch = {
    .name   = "scratch",
    .desc   = "ephemeral, unnamed buffer for quick notes",
    .init   = scratch_init,
    .deinit = NULL,
};
