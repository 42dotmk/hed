#include "emacs_keybinds.h"
#include "hed.h"

static int emacs_keybinds_init(void) {
    /* Esc/CR/Tab/BS + arrow drop/extend selection (shared with the
     * vscode keymap; modern emacs shift-select-mode semantics). */
    keybind_register_modeless_basics();

    /* Emacs motion (also drops selection). */
    mapi("<C-a>", kb_drop_bol, "beginning of line");
    mapi("<C-e>", kb_drop_eol, "end of line");
    mapi("<C-b>", kb_drop_left, "backward char");
    mapi("<C-f>", kb_drop_right, "forward char");
    mapi("<C-n>", kb_drop_down, "next line");
    mapi("<C-p>", kb_drop_up, "previous line");

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
    mapi("<M-x>", kb_enter_command_mode, "M-x (command mode)");
    mapi("<M-f>", kb_drop_word_r, "forward word");
    mapi("<M-b>", kb_drop_word_l, "backward word");
    cmapi("<M-<>", "goto gg", "beginning of buffer");
    cmapi("<M->>", "goto G", "end of buffer");
    cmapi("<M-d>", "delete_eol", "kill word forward (approx)");
    mapi("<M-w>", kb_visual_yank_selection, "copy region");

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
