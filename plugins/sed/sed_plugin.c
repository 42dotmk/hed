/* sed plugin: `:sed <expr>` runs an external `sed` over the active
 * buffer's contents, replacing the buffer with the output.
 *
 * The mechanic itself (process spawn, buffer swap, dirty flag, cursor
 * clamp) lives in sed.c — see sed.h. This file just exposes the
 * command and wires registration into the plugin system. */

#include "hed.h"
#include "sed.h"

static void cmd_sed(const char *args) {
    BUF(buf)
    if (!args || !*args) {
        ed_set_status_message("Usage: :sed <expression>");
        return;
    }

    EdError err = sed_apply_to_buffer(buf, args);
    if (err != ED_OK) {
        /* Error message already set by sed_apply_to_buffer */
        return;
    }
}

static int sed_init(void) {
    cmd("sed", cmd_sed, "apply sed expression to buffer");
    return 0;
}

const Plugin plugin_sed = {
    .name   = "sed",
    .desc   = "apply a sed expression to the current buffer",
    .init   = sed_init,
    .deinit = NULL,
};
