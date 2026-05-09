#include "config.h"
#include "commands.h"

#include "keybinds.h"
#include "keybinds_builtins.h"
#include "auto_pair/auto_pair.h"
#include "autosave/autosave.h"
#include "claude/claude.h"
#include "clipboard/clipboard.h"
#include "copilot/copilot.h"
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
#include "shell/shell.h"
#include "smart_indent/smart_indent.h"
#include "tmux/tmux_plugin.h"
#include "treesitter/treesitter.h"
#include "viewmd/viewmd_plugin.h"
#include "vim_keybinds/vim_keybinds.h"
#include "vscode_keybinds/vscode_keybinds.h"
#include "whichkey/whichkey.h"
#include "yazi/yazi.h"

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
    plugin_load(&plugin_shell,            1);
    plugin_load(&plugin_tmux,             1);
    plugin_load(&plugin_claude,           1);
    plugin_load(&plugin_treesitter,       1);
    plugin_load(&plugin_scratch,          1);
    plugin_load(&plugin_sed,              1);
    plugin_load(&plugin_reload,           1);
    plugin_load(&plugin_session,          1);
    plugin_load(&plugin_multicursor,      1);
    plugin_load(&plugin_whichkey,         1);
    plugin_load(&plugin_yazi,             1);
    plugin_load(&plugin_copilot,          1);
    plugin_load(&plugin_autosave,         1);

    cmapn(" ag",    "fzf",                  "find files");
    cmapn("  ",    "fzf",                  "find files");
    cmapn(" bb",   "ls",                   "buffer list");
    cmapn(" bd",   "bd",                   "buffer delete");
    cmapn(" br",   "refresh",              "buffer refresh");
    cmapn(" ff",   "fzf",                  "find files");
    cmapn(" fn",   "new",                  "new buffer");
    cmapn(" fr",   "recent",               "recent files");
    cmapn(" fs",   "w",                    "save file");
    cmapn(" fc",   "c",                    "find commands");
    cmapn(" fm",   "keybinds",             "find keybinds");
    cmapn(" cf",   "fmt",                  "format code");
    cmapn(" qq",   "q!",                   "quit (force)");
    cmapn(" rm",   "shell make",           "run make");
    cmapn(" mm",   "shell make",           "run make");
    cmapn(" mt",   "shell make test",      "run make test");
    cmapn(" nn",   "shell --skipwait nnn", "open nnn");
    cmapn(" dd",   "e .",                  "open cwd in dired");
    cmapn(" sd",   "rg",                   "ripgrep");
    cmapn(" ss",   "ssearch",              "buffer search");
    cmapn(" sa",   "rgword",               "rg word under cursor");
    cmapn("<C-*>", "rgword",               "rg word under cursor");
    cmapn(" tw",   "wrap",                 "toggle wordwrap");
    cmapn(" tt",   "tmux_toggle",          "toggle tmux pane");
    cmapn(" tT",   "tmux_kill",            "kill tmux pane");
    cmapn(" gg",   "git",                  "lazygit");
    cmapn(" tl",   "ln",                   "toggle line numbers");
    cmapn(" wd",   "wclose",               "close window");
    cmapn(" ws",   "split",                "split horizontal");
    cmapn(" wv",   "vsplit",               "split vertical");
    cmapn(" ww",   "wfocus",               "cycle window focus");
    cmapn(" wh",   "wh",                   "focus left");
    cmapn(" wj",   "wj",                   "focus down");
    cmapn(" wk",   "wk",                   "focus up");
    cmapn(" wl",   "wl",                   "focus right");
    cmapn(" ts",   "tmux_send_line",       "send paragraph to tmux");
    cmapn(" ai",   "claude_toggle",        "toggle Claude pane");
    mapn (" dl",   kb_del_right,           "del win right");
    mapn (" dh",   kb_del_left,            "del win left");
    mapn (" dj",   kb_del_down,            "del win down");
    mapn (" dk",   kb_del_up,              "del win up");
    cmapn(" rr",   "reload",               "reload editor");
    cmapn(" rp",   "viewmd",               "markdown preview");
    cmapn(" tq",   "ctoggle",              "toggle quickfix");
    cmapn("<C-n>", "cnext",                "quickfix next");
    cmapn("<C-p>", "cprev",                "quickfix prev");
    cmapn("gn",    "cnext",                "quickfix next");
    cmapn("gp",    "cprev",                "quickfix prev");
    cmapn("gr",    "rgword",               "rg word under cursor");
    cmapn("gd",    "tag",                  "goto tag");
    cmapn("K",     "lsp_hover",            "lsp hover");
    cmapn(" fh",   "hfzf",                 "history fzf");
    cmapn(" fj",   "jfzf",                 "jump-list fzf");
    cmapn(" fy",   "yazi",                 "pick file with yazi");
    cmapn(" z",    "scratch",              "scratch buffer");
    cmapn("<C-s>", "shell", "open shell prompt");
}
