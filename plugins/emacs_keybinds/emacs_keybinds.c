#include "emacs_keybinds.h"
#include "hed.h"

static int emacs_keybinds_init(void) {
    /* Esc/CR/Tab/BS + arrow drop/extend selection (shared with the
     * vscode keymap; modern emacs shift-select-mode semantics). */
    keybind_register_modeless_basics();

    /* Emacs motion (also drops selection). */
    cmapi("<C-a>", "goto 0", "beginning of line");
    cmapi("<C-e>", "goto $", "end of line");
    cmapi("<C-b>", "goto left", "backward char");
    cmapi("<C-f>", "goto right", "forward char");
    cmapi("<C-n>", "goto down", "next line");
    cmapi("<C-p>", "goto up", "previous line");

    /* Editing */
    cmapi("<C-d>", "delete_char", "delete char forward");
    cmapi("<C-k>", "delete_eol", "kill to end of line");
    cmapi("<C-y>", "put", "yank (paste)");

    /* Search / cancel */
    cmapi("<C-s>", "search", "isearch forward");
    cmapi("<C-r>", "search", "isearch backward (TODO)");
    mapv("<C-g>", kb_visual_escape, "cancel selection");

    /* C-x prefix cluster */
    cmapi("<C-x><C-s>", "w", "save");
    cmapi("<C-x><C-c>", "q", "quit");
    cmapi("<C-x><C-f>", "fzf", "find files");
    cmapi("<C-x>b", "ls", "switch buffer");
    cmapi("<C-x>k", "bd", "kill buffer");
    cmapi("<C-x>0", "wclose", "close window");
    cmapi("<C-x>2", "split", "split horizontal");
    cmapi("<C-x>3", "vsplit", "split vertical");
    cmapi("<C-x>o", "wfocus", "other window");
    cmapi("<C-x>u", "undo", "undo");

    /* Meta bindings (real M-keys via input layer) */
    cmapi("<M-x>", "prompt", "M-x (command mode)");
    cmapi("<M-f>", "goto w", "forward word");
    cmapi("<M-b>", "goto b", "backward word");
    cmapi("<M-<>", "goto gg", "beginning of buffer");
    cmapi("<M->>", "goto G", "end of buffer");
    cmapi("<M-d>", "delete_word_right", "kill word forward");
    cmapv("<M-w>", "yank", "copy region");

    cmapv("<C-w>", "delete", "kill region (cut)");

    ed_set_modeless(1);
    return 0;
}

const Plugin plugin_emacs_keybinds = {
    .name = "emacs_keybinds",
    .desc = "Emacs-flavored keymap (modal-bound)",
    .init = emacs_keybinds_init,
    .deinit = NULL,
};
