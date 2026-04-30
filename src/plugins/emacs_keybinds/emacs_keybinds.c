/* emacs_keybinds plugin: Emacs-flavored keymap.
 *
 * Provides Emacs muscle memory (C-a/C-e/C-n/C-p/C-b/C-f/C-d/C-k/C-y/C-s,
 * C-x prefix cluster, M-f/M-b/M-w/M-d/M-x/M-< /M->) and effectively turns
 * hed modeless via an always-insert mode hook.
 *
 * Conflicts with vim_keybinds in normal mode (both want C-d/C-n/C-p/C-u/
 * C-v/C-o). Do not enable both simultaneously. */

#include "../plugin.h"
#include "emacs_keybinds.h"
#include "hed.h"
#include "keybinds_builtins.h"

/* --- minimal motion helpers (raw cursor manipulation) --- */

static void emacs_bol(void) {
    Window *win = window_cur();
    if (!win) return;
    win->cursor.x = 0;
}

static void emacs_eol(void) {
    Window *win = window_cur();
    Buffer *buf = buf_cur();
    if (!win || !buf) return;
    if (win->cursor.y < 0 || win->cursor.y >= buf->num_rows) return;
    win->cursor.x = (int)buf->rows[win->cursor.y].chars.len;
}

static void emacs_buf_start(void) {
    Window *win = window_cur();
    if (!win) return;
    win->cursor.y = 0;
    win->cursor.x = 0;
    win->row_offset = 0;
}

static void emacs_buf_end(void) {
    Window *win = window_cur();
    Buffer *buf = buf_cur();
    if (!win || !buf || buf->num_rows == 0) return;
    win->cursor.y = buf->num_rows - 1;
    win->cursor.x = (int)buf->rows[win->cursor.y].chars.len;
}

/* C-g: cancel — exit visual/command back to insert. With the always-insert
 * hook below, we never sit in normal mode anyway. */
static void emacs_cancel(void) {
    if (E.mode == MODE_VISUAL || E.mode == MODE_VISUAL_LINE ||
        E.mode == MODE_VISUAL_BLOCK) {
        kb_visual_escape();
    }
    /* Mode change hook will pull us back to insert if we land in normal. */
}

/* Modeless gate. When 1, the always-insert hook bounces NORMAL → INSERT.
 * When 0, the hook is a no-op (lets a co-installed modal keymap work). */
static int g_modeless = 0;
static int suppress_remode = 0; /* recursion guard */

void emacs_keybinds_set_modeless(int on) {
    g_modeless = on ? 1 : 0;
    if (g_modeless && E.mode == MODE_NORMAL) {
        suppress_remode = 1;
        ed_set_mode(MODE_INSERT);
        suppress_remode = 0;
    }
}

int emacs_keybinds_is_modeless(void) {
    return g_modeless;
}

static void hook_always_insert(const HookModeEvent *e) {
    if (!e) return;
    if (!g_modeless) return;
    if (suppress_remode) return;
    if (e->new_mode != MODE_NORMAL) return;
    suppress_remode = 1;
    ed_set_mode(MODE_INSERT);
    suppress_remode = 0;
}

/* Kill region (C-w in visual): emacs cuts the active region. Maps to
 * the existing visual-delete which already yanks-then-deletes. */
static void emacs_kill_region(void) {
    kb_visual_delete_selection();
}

/* Helper for registering insert-mode command keybinds (no cmapi macro). */
static void cmapi(const char *seq, const char *cmd) {
    keybind_register_command(MODE_INSERT, seq, cmd);
}

static int emacs_keybinds_init(void) {
    /* === Insert mode (where typing happens) === */

    /* Motion */
    mapi("<C-a>", emacs_bol,        "beginning of line");
    mapi("<C-e>", emacs_eol,        "end of line");
    mapi("<C-b>", kb_move_left,     "backward char");
    mapi("<C-f>", kb_move_right,    "forward char");
    mapi("<C-n>", kb_move_down,     "next line");
    mapi("<C-p>", kb_move_up,       "previous line");

    /* Editing */
    mapi("<C-d>", kb_delete_char,         "delete char forward");
    mapi("<C-k>", kb_delete_to_line_end,  "kill to end of line");
    mapi("<C-y>", kb_paste,               "yank (paste)");

    /* Search */
    mapi("<C-s>", kb_search_prompt,  "isearch forward");
    mapi("<C-r>", kb_search_prompt,  "isearch backward (TODO)");

    /* Cancel */
    mapi("<C-g>", emacs_cancel, "cancel");

    /* C-x prefix cluster (multi-key sequences) */
    cmapi("<C-x><C-s>", "w");           /* save */
    cmapi("<C-x><C-c>", "q");           /* quit */
    cmapi("<C-x><C-f>", "fzf");         /* find file (uses fzf picker) */
    cmapi("<C-x>b",     "fzf");         /* switch buffer */
    cmapi("<C-x>k",     "bd");          /* kill buffer */
    cmapi("<C-x>0",     "wclose");      /* delete-window */
    cmapi("<C-x>2",     "split");       /* split-window-below */
    cmapi("<C-x>3",     "vsplit");      /* split-window-right */
    cmapi("<C-x>o",     "wfocus");      /* other-window */
    cmapi("<C-x>u",     "undo");        /* undo */

    /* Meta bindings — real M-keys, now that the input layer supports them. */
    mapi("<M-x>", kb_enter_command_mode, "M-x (command mode)");
    mapi("<M-f>", kb_para_next,          "forward word/paragraph");
    mapi("<M-b>", kb_para_prev,          "backward word/paragraph");
    mapi("<M-<>", emacs_buf_start,       "beginning of buffer");
    mapi("<M->>", emacs_buf_end,         "end of buffer");
    mapi("<M-d>", kb_delete_to_line_end, "kill word forward (approx)");
    mapi("<M-w>", kb_visual_yank_selection, "copy region (approx)");

    /* === Normal mode mirrors (limited — vim_keybinds owns most of these) === */

    mapn("<C-a>", emacs_bol,        "beginning of line");
    mapn("<C-e>", emacs_eol,        "end of line");
    mapn("<C-g>", emacs_cancel,     "cancel");

    /* === Visual mode === */

    mapv("<C-w>", emacs_kill_region, "kill region (cut)");
    mapv("<C-g>", emacs_cancel,      "cancel selection");

    /* Make the editor effectively modeless. */
    hook_register_mode(HOOK_MODE_CHANGE, hook_always_insert);
    emacs_keybinds_set_modeless(1);

    return 0;
}

const Plugin plugin_emacs_keybinds = {
    .name   = "emacs_keybinds",
    .desc   = "Emacs-flavored keymap (modal-bound, no Meta support)",
    .init   = emacs_keybinds_init,
    .deinit = NULL,
};
