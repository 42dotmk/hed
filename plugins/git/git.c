/* git plugin: launches lazygit as a full-screen TUI, like fzf —
 * temporarily leaves raw mode and repaints on return. */

#include "hed.h"

static void cmd_git(const char *args) {
    (void)args;
    int status = term_cmd_run_interactive("lazygit", false);
    if (status == 0) {
        ed_set_status_message("lazygit exited");
    } else if (status == -1) {
        ed_set_status_message("failed to run lazygit");
    } else {
        ed_set_status_message("lazygit exited with status %d", status);
    }
    ed_render_frame();
}

static int git_init(void) {
    cmd("git", cmd_git, "run lazygit");
    return 0;
}

const Plugin plugin_git = {
    .name   = "git",
    .desc   = "lazygit launcher",
    .init   = git_init,
    .deinit = NULL,
};
