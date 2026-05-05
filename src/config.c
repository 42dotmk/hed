#include "config.h"
#include "commands.h"
#include "keybinds.h"
#include "keybinds_builtins.h"
#include "auto_pair/auto_pair.h"
#include "claude/claude.h"
#include "clipboard/clipboard.h"
#include "core/core.h"
#include "dired/dired_plugin.h"
#include "emacs_keybinds/emacs_keybinds.h"
#include "fmt/fmt.h"
#include "keymap/keymap.h"
#include "lsp/lsp_plugin.h"
#include "multicursor/multicursor.h"
#include "plugin.h"
#include "quickfix_preview/quickfix_preview.h"
#include "reload/reload.h"
#include "scratch/scratch.h"
#include "sed/sed_plugin.h"
#include "session/session_plugin.h"
#include "smart_indent/smart_indent.h"
#include "tmux/tmux_plugin.h"
#include "treesitter/treesitter.h"
#include "viewmd/viewmd_plugin.h"
#include "vim_keybinds/vim_keybinds.h"
#include "vscode_keybinds/vscode_keybinds.h"
#include "whichkey/whichkey.h"

void config_init(void) {
    plugin_load(&plugin_core,             1);
    plugin_load(&plugin_vim_keybinds,     1);
    plugin_load(&plugin_emacs_keybinds,   0);
    plugin_load(&plugin_vscode_keybinds,  0);
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
    plugin_load(&plugin_claude,           1);
    plugin_load(&plugin_treesitter,       1);
    plugin_load(&plugin_scratch,          1);
    plugin_load(&plugin_sed,              1);
    plugin_load(&plugin_reload,           1);
    plugin_load(&plugin_session,          1);
    plugin_load(&plugin_multicursor,      1);
    plugin_load(&plugin_whichkey,         1);

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
    cmapn(" ai", "claude_toggle");
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
    cmapn(" z", "scratch");
}
