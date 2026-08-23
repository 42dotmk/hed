/* scratch plugin: an ephemeral, unnamed buffer reachable in one keystroke.
 *
 * `:scratch` opens (or focuses) a vertical split showing the shared
 * "[scratch]" buffer. Contents persist for the lifetime of the editor
 * session. The buffer has no filename and is kept non-dirty so `:q`
 * closes the split without complaint. */

#include "hed.h"

#define SCRATCH_TITLE "[scratch]"

static int scratch_get(void) {
    BufSpecial spec = {.name = SCRATCH_TITLE};
    return buf_special_get(&spec, NULL);
}

static void cmd_scratch(const char *args) {
    (void)args;
    int idx = scratch_get();
    if (idx < 0) {
        ed_set_status_message("scratch: failed to create buffer");
        return;
    }
    buf_special_show_split(idx, 1);
}

/* Fallback hook for the editor: when no buffers remain, return the
 * scratch buffer instead of letting core create a nameless empty one. */
static int scratch_fallback_buf(void) { return scratch_get(); }

static int scratch_init(void) {
    cmd("scratch", cmd_scratch, "open/focus the scratch buffer in a vsplit");
    E.fallback_buf_fn = scratch_fallback_buf;
    return 0;
}

const Plugin plugin_scratch = {
    .name = "scratch",
    .desc = "ephemeral, unnamed buffer for quick notes",
    .init = scratch_init,
    .deinit = NULL,
};
