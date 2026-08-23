#include "vscode_keybinds.h"
#include "hed.h"

static int vscode_keybinds_init(void) {
    /* Esc/CR/Tab/BS + arrow drop/extend selection (shared with the * emacs
     * keymap). */
    keybind_register_modeless_basics();

    cmapi("<Del>", "delete_forward", "delete forward");
    cmapv("<Del>", "delete", "delete selection");
    cmapv("<BS>", "delete", "delete selection");
    cmapi("<C-h>", "delete_word_left", "delete word left (Ctrl+Backspace)");
    cmapi("<C-Del>", "delete_word_right", "delete word right");

    /* Whole-buffer / line selection. */
    cmapi("<C-a>", "select ae", "select all");
    cmapv("<C-a>", "select ae", "select all");
    cmapi("<C-l>", "select_line", "select line");
    cmapv("<C-l>", "select_line", "extend selection by a line");

    /* File-edge motion (Ctrl+Home/End) and selection (+Shift). */
    cmapi("<C-Home>", "goto gg", "start of file");
    cmapi("<C-End>", "goto G", "end of file");
    cmapv("<C-Home>", "goto gg", "start of file");
    cmapv("<C-End>", "goto G", "end of file");
    cmapi("<C-S-Home>", "extend gg", "select to start of file");
    cmapi("<C-S-End>", "extend G", "select to end of file");
    cmapv("<C-S-Home>", "extend gg", "extend to start of file");
    cmapv("<C-S-End>", "extend G", "extend to end of file");

    /* Paging. */
    cmapi("<PageUp>", "goto pageup", "page up");
    cmapi("<PageDown>", "goto pagedown", "page down");
    cmapv("<PageUp>", "goto pageup", "page up");
    cmapv("<PageDown>", "goto pagedown", "page down");
    cmapi("<S-PageUp>", "extend pageup", "select page up");
    cmapi("<S-PageDown>", "extend pagedown", "select page down");
    cmapv("<S-PageUp>", "extend pageup", "extend page up");
    cmapv("<S-PageDown>", "extend pagedown", "extend page down");

    /* File / window / buffer. */
    cmapi("<C-s>", "w", "save");
    cmapi("<C-n>", "new", "new buffer");
    cmapi("<C-o>", "fzf", "open file");
    cmapi("<C-p>", "fzf", "quick open");
    cmapi("<C-e>", "recent", "recent files");
    cmapi("<C-w>", "wclose", "close window");
    cmapi("<C-\\>", "vsplit", "split editor");
    cmapi("<M-\\>", "vsplit", "split vertical");
    cmapi("<M-->", "split", "split horizontal");
    cmapi("<M-n>", "bn", "next buffer");
    cmapi("<M-N>", "bp", "prev buffer");
    cmapi("<C-PageDown>", "bn", "next buffer (Ctrl+PgDn)");
    cmapi("<C-PageUp>", "bp", "prev buffer (Ctrl+PgUp)");
    cmapi("<C-b>", "e .", "file explorer (dired)");
    cmapi("<C-k>s", "wa", "save all");
    cmapi("<C-k><C-w>", "qa", "close all (quit if nothing unsaved)");
    cmapi("<C-k><C-Left>", "wh", "focus left editor group");
    cmapi("<C-k><C-Right>", "wl", "focus right editor group");
    cmapi("<C-k>g", "git", "source control (lazygit)");
    cmapi("<M-t>", "tmux_toggle", "toggle terminal (tmux pane)");
    cmapi("<M-z>", "wrap", "toggle word wrap");

    /* Command palette. F1 / Alt+P because terminals can't deliver
     * Ctrl+Shift+P. */
    cmapi("<M-p>", "prompt", "command palette");
    cmapi("<F1>", "prompt", "command palette");

    /* Undo / redo / clipboard. */
    cmapi("<C-z>", "undo", "undo");
    cmapi("<C-y>", "redo", "redo");
    cmapi("<C-v>", "put", "paste");
    /* No selection: line-wise copy/cut (VSCode semantics). The cmapn
     * duplicates keep these working if modeless is toggled off. */
    cmapi("<C-c>", "yank_line", "copy line");
    cmapi("<C-x>", "delete_line", "cut line");
    cmapn("<C-c>", "yank_line", "copy line");
    cmapn("<C-x>", "delete_line", "cut line");
    cmapv("<C-c>", "yank", "copy selection");
    cmapv("<C-x>", "delete", "cut selection");

    /* Find & navigate. */
    cmapi("<C-f>", "search", "find in file");
    cmapi("<C-S-f>", "rg", "search in workspace");
    cmapi("<F3>", "search_next", "find next");
    cmapi("<S-F3>", "search_prev", "find previous");
    cmapi("<C-g>", "prompt goto", "go to line");
    cmapi("<M-Left>", "jump_back", "navigate back");
    cmapi("<M-Right>", "jump_forward", "navigate forward");
    cmapi("<F12>", "tag", "go to definition (ctags)");
    cmapi("<C-t>", "tag", "go to symbol (ctags)");
    cmapi("<F8>", "cnext", "next problem (quickfix)");
    cmapi("<S-F8>", "cprev", "previous problem (quickfix)");
    cmapi("<M-m>", "ctoggle", "toggle problems panel (quickfix)");
    cmapi("<M-l>", "mc_match_all", "select all occurrences");

    /* Multi-cursor (VSCode Ctrl+D family, via the multicursor plugin). */
    cmapi("<C-d>", "mc_next_match", "add cursor at next occurrence");
    cmapv("<C-d>", "mc_next_match", "add cursor at next match of selection");
    cmapi("<C-k><C-d>", "mc_skip", "skip occurrence, take next");
    cmapi("<M-C-Up>", "mc_add_above", "add cursor above");
    cmapi("<M-C-Down>", "mc_add_below", "add cursor below");
    cmapi("<C-k><C-s>", "mc_sync toggle", "toggle synced multi-cursor edits");
    cmapi("<C-k><Esc>", "mc_clear", "clear extra cursors");

    /* Line operations. */
    cmapi("<M-Up>", "move_line_up", "move line up");
    cmapi("<M-Down>", "move_line_down", "move line down");
    cmapi("<M-S-Down>", "duplicate_line", "duplicate line");
    cmapi("<C-]>", "indent", "indent line");
    cmapi("<S-Tab>", "unindent", "unindent line");
    cmapv("<C-]>", "indent", "indent line");
    cmapv("<S-Tab>", "unindent", "unindent line");
    cmapi("<C-_>", "toggle_comment", "toggle comment (Ctrl+/)");
    cmapv("<C-_>", "toggle_comment", "toggle comment (Ctrl+/)");
    cmapi("<M-/>", "toggle_comment", "toggle comment");
    cmapi("<M-F>", "fmt", "format document (Shift+Alt+F)");

    /* Language niceties (Ctrl+K chords, matching VSCode). */
    cmapi("<C-k><C-i>", "lsp_hover", "show hover (lsp)");
    cmapi("<C-k>v", "viewmd", "markdown preview");

    /* Scrolling: VSCode Ctrl+Up/Down scrolls without moving the
     * cursor; multicursor keeps its VSCode-faithful Ctrl+Alt+Up/Down
     * (this keymap's init re-runs on :keymap, so these win the
     * last-write-wins race against multicursor's global binds). */
    cmapi("<C-Up>", "scroll_line_up", "scroll up one line");
    cmapi("<C-Down>", "scroll_line_down", "scroll down one line");

    /* Folding (VSCode Ctrl+K chords; Ctrl+0 isn't deliverable, so the
     * fold-all chord is Ctrl+K then plain 0). */
    cmapi("<C-k><C-l>", "foldtoggle", "toggle fold");
    cmapi("<C-k><C-j>", "foldopenall", "unfold all");
    cmapi("<C-k>0", "foldcloseall", "fold all");

    /* Word motion. */
    cmapi("<Home>", "goto 0", "beginning of line");
    cmapi("<End>", "goto $", "end of line");
    cmapi("<C-Left>", "goto b", "previous word");
    cmapi("<C-Right>", "goto w", "next word");
    cmapv("<C-Left>", "goto b", "previous word");
    cmapv("<C-Right>", "goto w", "next word");

    ed_set_modeless(1);
    return 0;
}

const Plugin plugin_vscode_keybinds = {
    .name = "vscode_keybinds",
    .desc = "VSCode-flavored keymap (modeless, Ctrl-key oriented)",
    .init = vscode_keybinds_init,
    .deinit = NULL,
};
