/* viewmd plugin: activates the markdown live-preview integration.
 *
 * Implementation lives next to this file in viewmd_impl.c. This plugin
 * owns activation: the buffer/char/line hooks plus the `:viewmd` command. */

#include "../plugin.h"
#include "hed.h"
#include "viewmd.h"

static int viewmd_plugin_init(void) {
    viewmd_init();
    cmd("viewmd", cmd_viewmd_preview, "markdown live preview");
    return 0;
}

const Plugin plugin_viewmd = {
    .name   = "viewmd",
    .desc   = "markdown live preview",
    .init   = viewmd_plugin_init,
    .deinit = NULL,
};
