/* ctags plugin: :tag lookup driven by utils/ctags.c.
 * Bind `gd` (or whatever you prefer) to :tag from config.c. */

#include "hed.h"
#include "ctags/tags.h"

static void cmd_tag(const char *args) {
    goto_tag(args && *args ? args : NULL);
    buf_center_screen();
}

static int ctags_init(void) {
    cmd("tag", cmd_tag, "jump to tag definition");
    return 0;
}

const Plugin plugin_ctags = {
    .name   = "ctags",
    .desc   = "ctags lookup (:tag)",
    .init   = ctags_init,
    .deinit = NULL,
};
