#include "config.h"
#include "hed.h"
#include "keybinds_builtins.h"
#include "plugins/auto_pair/auto_pair.h"
#include "plugins/clipboard/clipboard.h"
#include "plugins/core/core.h"
#include "plugins/dired/dired_plugin.h"
#include "plugins/emacs_keybinds/emacs_keybinds.h"
#include "plugins/fmt/fmt.h"
#include "plugins/keymap/keymap.h"
#include "plugins/lsp/lsp_plugin.h"
#include "plugins/plugin.h"
#include "plugins/quickfix_preview/quickfix_preview.h"
#include "plugins/smart_indent/smart_indent.h"
#include "plugins/tmux/tmux_plugin.h"
#include "plugins/viewmd/viewmd_plugin.h"
#include "plugins/vim_keybinds/vim_keybinds.h"

void config_init(void) {
    plugin_load(&plugin_core,             1);
    plugin_load(&plugin_vim_keybinds,     1);
    plugin_load(&plugin_emacs_keybinds,   0);
    plugin_load(&plugin_keymap,           1);
    plugin_load(&plugin_clipboard,        1);
    plugin_load(&plugin_quickfix_preview, 1);
    plugin_load(&plugin_dired,            1);
    plugin_load(&plugin_lsp,              1);
    plugin_load(&plugin_viewmd,           1);
    plugin_load(&plugin_auto_pair,        1);
    plugin_load(&plugin_smart_indent,     1);
    plugin_load(&plugin_fmt,              1);
    plugin_load(&plugin_tmux,             1);

    cmapn("  ", "fzf");
    cmapn(" bb", "ls");
    cmapn(" bd", "bd");
    cmapn(" br", "refresh");
    cmapn(" ff", "fzf");
    cmapn(" fn", "new");
    cmapn(" fr", "recent");
    cmapn(" fs", "w");
    cmapn(" fc", "c");
    cmapn(" fm", "keybinds");
    cmapn(" cf", "fmt");
    cmapn(" qq", "q!");
    cmapn(" rm", "shell make");
    cmapn(" mm", "shell make");
    cmapn(" mt", "shell make test");
    cmapn(" nn", "shell --skipwait nnn");
    cmapn(" dd", "e .");
    cmapn(" sd", "rg");
    cmapn(" ss", "ssearch");
    cmapn(" sa", "rgword");
    cmapn("<C-*>", "rgword");
    cmapn(" tw", "wrap");
    cmapn(" tt", "tmux_toggle");
    cmapn(" tT", "tmux_kill");
    cmapn(" gg", "git");
    cmapn(" tl", "ln");
    cmapn(" wd", "wclose");
    cmapn(" ws", "split");
    cmapn(" wv", "vsplit");
    cmapn(" ww", "wfocus");
    cmapn(" wh", "wh");
    cmapn(" wj", "wj");
    cmapn(" wk", "wk");
    cmapn(" wl", "wl");
    cmapn(" ts", "tmux_send_line");
    mapn(" dl", kb_del_right, "del win right");
    mapn(" dh", kb_del_left, "del win left");
    mapn(" dj", kb_del_down, "del win down");
    mapn(" dk", kb_del_up, "del win up");
    cmapn(" rr", "reload");
    cmapn(" rp", "viewmd");
    cmapn(" tq", "ctoggle");
    cmapn("<C-n>", "cnext");
    cmapn("<C-p>", "cprev");
    cmapn("gn", "cnext");
    cmapn("gp", "cprev");
    cmapn("gr", "rgword");
    cmapn("gd", "tag");
    cmapn("K", "lsp_hover");
    cmapn(" fh", "hfzf");
    cmapn(" fj", "jfzf");
}
