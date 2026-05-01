#include "vscode_keybinds.h"
#include "hed.h"

static int vscode_keybinds_init(void) {
    /* Universal insert-mode keys (don't rely on vim_keybinds). */
    mapi("<Esc>",   kb_insert_escape,    "exit insert (no-op when modeless)");
    mapi("<CR>",    kb_insert_newline,   "newline");
    mapi("<Tab>",   kb_insert_tab,       "insert tab");
    mapi("<BS>",    kb_insert_backspace, "backspace");
    mapv("<Esc>",   kb_visual_escape,    "exit visual");

    /* Plain motion: drop any active selection. Bound in both insert and
     * visual so an unmodified arrow exits the selection. */
    mapi("<Up>",    kb_drop_up,    "up");
    mapi("<Down>",  kb_drop_down,  "down");
    mapi("<Left>",  kb_drop_left,  "left");
    mapi("<Right>", kb_drop_right, "right");
    mapv("<Up>",    kb_drop_up,    "up");
    mapv("<Down>",  kb_drop_down,  "down");
    mapv("<Left>",  kb_drop_left,  "left");
    mapv("<Right>", kb_drop_right, "right");

    /* Shift+motion: enter (or extend) a selection. */
    mapi("<S-Up>",     kb_extend_up,    "select up");
    mapi("<S-Down>",   kb_extend_down,  "select down");
    mapi("<S-Left>",   kb_extend_left,  "select left");
    mapi("<S-Right>",  kb_extend_right, "select right");
    mapv("<S-Up>",     kb_extend_up,    "extend up");
    mapv("<S-Down>",   kb_extend_down,  "extend down");
    mapv("<S-Left>",   kb_extend_left,  "extend left");
    mapv("<S-Right>",  kb_extend_right, "extend right");

    /* Ctrl+Shift+arrow: word-wise select. */
    mapi("<C-S-Left>",  kb_extend_word_l, "select previous word");
    mapi("<C-S-Right>", kb_extend_word_r, "select next word");
    mapv("<C-S-Left>",  kb_extend_word_l, "extend previous word");
    mapv("<C-S-Right>", kb_extend_word_r, "extend next word");

    /* Shift+Home/End: select to bol/eol. */
    mapi("<S-Home>", kb_extend_bol, "select to bol");
    mapi("<S-End>",  kb_extend_eol, "select to eol");
    mapv("<S-Home>", kb_extend_bol, "extend to bol");
    mapv("<S-End>",  kb_extend_eol, "extend to eol");

    cmapi("<C-s>", "w");
    cmapi("<C-n>", "new");
    cmapi("<C-o>", "fzf");
    cmapi("<C-p>", "fzf");
    cmapi("<C-w>", "wclose");
    mapi("<M-p>", kb_enter_command_mode, "command palette");
    mapi("<F1>",  kb_enter_command_mode, "command palette");
    cmapi("<C-z>", "undo");
    cmapi("<C-y>", "redo");
    mapi("<C-f>", kb_search_prompt,         "find in file");
    cmapi("<C-S-f>", "rg");
    mapi("<C-d>", kb_find_under_cursor,     "select next occurrence");
    mapi("<C-g>", kb_search_prompt,         "go to line / search");
    mapi("<C-v>", kb_paste,                 "paste");
    mapn("<C-c>", kb_yank_line,             "copy line");
    mapn("<C-x>", kb_delete_line,           "cut line");
    mapv("<C-c>", kb_visual_yank_selection, "copy selection");
    mapv("<C-x>", kb_visual_delete_selection, "cut selection");
    mapi("<Home>",    kb_drop_bol,    "beginning of line");
    mapi("<End>",     kb_drop_eol,    "end of line");
    mapi("<C-Left>",  kb_drop_word_l, "previous word");
    mapi("<C-Right>", kb_drop_word_r, "next word");
    mapv("<C-Left>",  kb_drop_word_l, "previous word");
    mapv("<C-Right>", kb_drop_word_r, "next word");
    cmapi("<M-\\>", "vsplit");          
    cmapi("<M-->", "split");            
    cmapi("<M-n>", "bn");
    cmapi("<M-N>", "bp");
    mapi("<M-/>", buf_toggle_comment, "toggle comment");
    ed_set_modeless(1);
    return 0;
}

const Plugin plugin_vscode_keybinds = {
    .name   = "vscode_keybinds",
    .desc   = "VSCode-flavored keymap (modeless, Ctrl-key oriented)",
    .init   = vscode_keybinds_init,
    .deinit = NULL,
};
