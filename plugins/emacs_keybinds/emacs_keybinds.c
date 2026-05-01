#include "emacs_keybinds.h"
#include "hed.h"

static int emacs_keybinds_init(void) {
    /* Universal insert-mode keys (don't rely on vim_keybinds). */
    mapi("<Esc>", kb_insert_escape,    "exit insert (no-op when modeless)");
    mapi("<CR>",  kb_insert_newline,   "newline");
    mapi("<Tab>", kb_insert_tab,       "insert tab");
    mapi("<BS>",  kb_insert_backspace, "backspace");
    mapv("<Esc>", kb_visual_escape,    "exit visual");

    /* Plain motion: drops any active selection (modern emacs
     * shift-select-mode semantics). */
    mapi("<Up>",    kb_drop_up,    "up");
    mapi("<Down>",  kb_drop_down,  "down");
    mapi("<Left>",  kb_drop_left,  "left");
    mapi("<Right>", kb_drop_right, "right");
    mapv("<Up>",    kb_drop_up,    "up");
    mapv("<Down>",  kb_drop_down,  "down");
    mapv("<Left>",  kb_drop_left,  "left");
    mapv("<Right>", kb_drop_right, "right");

    /* Emacs motion (also drops selection). */
    mapi("<C-a>", kb_drop_bol,    "beginning of line");
    mapi("<C-e>", kb_drop_eol,    "end of line");
    mapi("<C-b>", kb_drop_left,   "backward char");
    mapi("<C-f>", kb_drop_right,  "forward char");
    mapi("<C-n>", kb_drop_down,   "next line");
    mapi("<C-p>", kb_drop_up,     "previous line");

    /* Shift+motion: enter / extend selection. (Most terminals don't
     * deliver Ctrl+Shift+letter distinctly, so we rely on CSI shift+
     * arrow / shift+Home/End / Ctrl-Shift+arrow which DO go through
     * the modifier-CSI path.) */
    mapi("<S-Up>",      kb_extend_up,     "select up");
    mapi("<S-Down>",    kb_extend_down,   "select down");
    mapi("<S-Left>",    kb_extend_left,   "select left");
    mapi("<S-Right>",   kb_extend_right,  "select right");
    mapv("<S-Up>",      kb_extend_up,     "extend up");
    mapv("<S-Down>",    kb_extend_down,   "extend down");
    mapv("<S-Left>",    kb_extend_left,   "extend left");
    mapv("<S-Right>",   kb_extend_right,  "extend right");
    mapi("<C-S-Left>",  kb_extend_word_l, "select previous word");
    mapi("<C-S-Right>", kb_extend_word_r, "select next word");
    mapv("<C-S-Left>",  kb_extend_word_l, "extend previous word");
    mapv("<C-S-Right>", kb_extend_word_r, "extend next word");
    mapi("<S-Home>",    kb_extend_bol,    "select to bol");
    mapi("<S-End>",     kb_extend_eol,    "select to eol");
    mapv("<S-Home>",    kb_extend_bol,    "extend to bol");
    mapv("<S-End>",     kb_extend_eol,    "extend to eol");

    /* Editing */
    mapi("<C-d>", kb_delete_char,        "delete char forward");
    mapi("<C-k>", kb_delete_to_line_end, "kill to end of line");
    mapi("<C-y>", kb_paste,              "yank (paste)");

    /* Search / cancel */
    mapi("<C-s>", kb_search_prompt, "isearch forward");
    mapi("<C-r>", kb_search_prompt, "isearch backward (TODO)");
    mapv("<C-g>", kb_visual_escape, "cancel selection");

    /* C-x prefix cluster */
    cmapi("<C-x><C-s>", "w");
    cmapi("<C-x><C-c>", "q");
    cmapi("<C-x><C-f>", "fzf");
    cmapi("<C-x>b",     "fzf");
    cmapi("<C-x>k",     "bd");
    cmapi("<C-x>0",     "wclose");
    cmapi("<C-x>2",     "split");
    cmapi("<C-x>3",     "vsplit");
    cmapi("<C-x>o",     "wfocus");
    cmapi("<C-x>u",     "undo");

    /* Meta bindings (real M-keys via input layer) */
    mapi("<M-x>",  kb_enter_command_mode,  "M-x (command mode)");
    mapi("<M-f>",  kb_drop_word_r,         "forward word");
    mapi("<M-b>",  kb_drop_word_l,         "backward word");
    mapi("<M-<>",  kb_goto_file_start,     "beginning of buffer");
    mapi("<M->>",  kb_goto_file_end,       "end of buffer");
    mapi("<M-d>",  kb_delete_to_line_end,  "kill word forward (approx)");
    mapi("<M-w>",  kb_visual_yank_selection, "copy region");

    mapv("<C-w>", kb_visual_delete_selection, "kill region (cut)");

    ed_set_modeless(1);
    return 0;
}

const Plugin plugin_emacs_keybinds = {
    .name   = "emacs_keybinds",
    .desc   = "Emacs-flavored keymap (modal-bound)",
    .init   = emacs_keybinds_init,
    .deinit = NULL,
};
