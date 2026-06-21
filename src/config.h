#ifndef HED_CONFIG_H
#define HED_CONFIG_H
#include "input/keybinds.h"
#include "input/keybinds_builtins.h"

#include "auto_pair/auto_pair.h"
#include "autosave/autosave.h"
#include "aishell/aishell.h"
#include "clipboard/clipboard.h"
#include "copilot/copilot.h"
#include "core/core.h"
#include "ctags/ctags.h"
#include "dired/dired_plugin.h"
#include "emacs_keybinds/emacs_keybinds.h"
#include "fmt/fmt.h"
#include "folds/folds.h"
#include "git/git.h"
#include "keymap/keymap.h"
#include "lsp/lsp_plugin.h"
#include "mail/mail.h"
#include "mail_git_patch/mail_git_patch.h"
#include "man/man.h"
#include "mcp_server/mcp_server.h"
#include "mouse/mouse.h"
#include "multicursor/multicursor.h"
#include "open/open.h"
#include "pickers/pickers.h"
#include "plugin.h"
#include "quickfix_preview/quickfix_preview.h"
#include "reload/reload.h"
#include "scratch/scratch.h"
#include "search/search.h"
#include "selectlist/selectlist.h"
#include "sed/sed_plugin.h"
#include "session/session_plugin.h"
#include "shell/shell.h"
#include "smart_indent/smart_indent.h"
#include "tasks/tasks.h"
#include "tmux/tmux_plugin.h"
#include "translate/translate.h"
#include "hed_themes/hed_themes.h"
#include "markdown/markdown.h"
#include "treesitter/treesitter.h"
#include "viewmd/viewmd_plugin.h"
#include "vim_keybinds/vim_keybinds.h"
#include "vscode_keybinds/vscode_keybinds.h"
#include "whichkey/whichkey.h"
#include "yazi/yazi.h"

/* Stock plugin set. 1 = enabled now, 0 = loaded but inactive
 * (available for runtime swap, e.g. via :keymap). */
static void config_load_default_plugins(void) {
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
    plugin_load(&plugin_folds,            1);
    plugin_load(&plugin_shell,            1);
    plugin_load(&plugin_tmux,             1);
    plugin_load(&plugin_translate,        1);
    plugin_load(&plugin_aishell,          1);
    plugin_load(&plugin_treesitter,       1);
    plugin_load(&plugin_hed_themes,       1);
    plugin_load(&plugin_markdown,         1);
    plugin_load(&plugin_tasks,            1);
    plugin_load(&plugin_scratch,          1);
    plugin_load(&plugin_search,           1);
    plugin_load(&plugin_selectlist,       1);
    plugin_load(&plugin_sed,              1);
    plugin_load(&plugin_reload,           1);
    plugin_load(&plugin_session,          1);
    plugin_load(&plugin_multicursor,      1);
    plugin_load(&plugin_mouse,            1);
    plugin_load(&plugin_whichkey,         1);
    plugin_load(&plugin_yazi,             1);
    plugin_load(&plugin_copilot,          1);
    plugin_load(&plugin_autosave,         1);
    plugin_load(&plugin_ctags,            1);
    plugin_load(&plugin_git,              1);
    plugin_load(&plugin_pickers,          1);
    plugin_load(&plugin_mail,             1);
    plugin_load(&plugin_mail_git_patch,   1);
    plugin_load(&plugin_man,              1);
    plugin_load(&plugin_mcp_server,       1);
    plugin_load(&plugin_open,             1);
}

/* Default settings + leader keybind cluster. Last-write-wins: the
 * user config runs after this, so anything here can be overridden
 * by rebinding the same key there. */
static void config_load_defaults(void) {
    theme_activate("tokyo-night");
    translate_set_default_target("en");

    cmapn("  ",    "fzf",                  "find files");
    cmapn(" bb",   "ls",                   "buffer list");
    cmapn(" bo",   "b#",                   "alt buffer");
    cmapn(" bn",   "bn",                   "next buffer");
    cmapn(" bp",   "bp",                   "prev buffer");
    cmapn(" bd",   "bd",                   "buffer delete");
    cmapn(" bD",   "bd!",                  "buffer delete (force)");
    cmapn(" br",   "refresh",              "buffer refresh");
    cmapn(" ff",   "fzf",                  "find files");
    cmapn(" fn",   "new",                  "new buffer");
    cmapn(" fr",   "recent",               "recent files");
    cmapn(" fs",   "w",                    "save file");
    cmapn(" fc",   "c",                    "find commands");
    cmapn(" fm",   "keybinds",             "find keybinds");
    cmapn(" hm",   "man",                  "man pages");
    cmapn(" cf",   "fmt",                  "format code");
    cmapn(" qq",   "q!",                   "quit (force)");
    cmapn(" rm",   "shell make",           "run make");
    cmapn(" rt",   "shell make test",      "run make test");
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
    cmapn(" ai",   "ai_toggle",            "toggle AI shell pane");
    mapn (" dl",   kb_del_right,           "del win right");
    mapn (" dh",   kb_del_left,            "del win left");
    mapn (" dj",   kb_del_down,            "del win down");
    mapn (" dk",   kb_del_up,              "del win up");
    cmapn(" rr",   "reload",               "reload editor");
    cmapn(" rp",   "viewmd",               "markdown preview");
    cmapn(" tq",   "ctoggle",              "toggle quickfix");
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
    cmapn(" lt", "translate",          "translate buffer/selection");
    cmapv(" lt", "translate",          "translate selection");
}

/* Optional user config: ~/.config/hed/config.c (or USER_CONFIG=path).
 * The Makefile compiles it in when present; it runs after the stock
 * defaults, so plugin_load and keybinds there are additive and
 * override defaults via last-write-wins. Declared weak so the build
 * links cleanly when no user config exists. */
void config_user_init(void) __attribute__((weak));

void config_init(void) {
    config_load_default_plugins();
    config_load_defaults();
    if (config_user_init) config_user_init();
}
#endif
