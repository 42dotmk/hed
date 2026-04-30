/* core plugin: ships the default command set and the small handful of
 * editor-wide hooks (cursor shape on mode change, undo grouping, smart
 * indent, auto-pair). Commands owned by other plugins (lsp, viewmd,
 * dired's :keymap, etc.) live with their plugin, not here. */

#include "plugin.h"
#include "cmd_builtins.h"
#include "hed.h"
#include "hook_builtins.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* :goto — move the cursor by line number, by motion symbol, or by motion
 * symbol with a count.
 *
 *   :goto 42        → jump to line 42
 *   :goto $         → end of line (any registered text object/motion)
 *   :goto j 5       → move down 5 lines
 *   :goto w 3       → forward 3 words
 *
 * Motions are looked up via textobj_lookup, so anything registered with
 * textobj_register works (h/j/k/l, w/b/e, $/0, gg/G, {/}, ...). */
static void cmd_goto(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("Usage: :goto <line> | <motion> [count]");
        return;
    }
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!buf || !win) return;

    while (*args == ' ' || *args == '\t') args++;

    /* If the whole arg parses as a positive integer, treat as line jump. */
    {
        char *end;
        long n = strtol(args, &end, 10);
        const char *trail = end;
        while (*trail == ' ' || *trail == '\t') trail++;
        if (end != args && *trail == '\0' && n >= 1) {
            if (n > buf->num_rows) n = buf->num_rows;
            win->cursor.y = (int)n - 1;
            win->cursor.x = 0;
            return;
        }
    }

    /* Otherwise: motion symbol, optionally followed by a count. */
    char motion[32];
    int mi = 0;
    while (*args && *args != ' ' && *args != '\t' && mi < (int)sizeof(motion) - 1) {
        motion[mi++] = *args++;
    }
    motion[mi] = '\0';
    if (mi == 0) {
        ed_set_status_message(":goto: empty motion");
        return;
    }

    while (*args == ' ' || *args == '\t') args++;
    int count = 1;
    if (*args) {
        char *end;
        long c = strtol(args, &end, 10);
        if (end != args && c >= 1) count = (int)c;
    }

    for (int i = 0; i < count; i++) {
        TextSelection sel;
        if (!textobj_lookup(motion, buf, win->cursor.y, win->cursor.x, &sel)) {
            ed_set_status_message(":goto: unknown motion '%s'", motion);
            return;
        }
        win->cursor.y = sel.cursor.line;
        win->cursor.x = sel.cursor.col;
    }
}

/* :modeless on|off|toggle — toggle the global "always-insert" redirect.
 * When on, NORMAL mode is unreachable. */
static void cmd_modeless(const char *args) {
    int target;
    if (!args || !*args || strcmp(args, "toggle") == 0) {
        target = !ed_is_modeless();
    } else if (strcmp(args, "on") == 0 || strcmp(args, "1") == 0) {
        target = 1;
    } else if (strcmp(args, "off") == 0 || strcmp(args, "0") == 0) {
        target = 0;
    } else {
        ed_set_status_message("modeless: usage on|off|toggle");
        return;
    }
    ed_set_modeless(target);
    ed_set_status_message("modeless: %s", target ? "on" : "off");
}

static void register_commands(void) {
    cmd("q", cmd_quit, "quit");
    cmd("q!", cmd_quit_force, "quit!");
    cmd("quit", cmd_quit, "quit");
    cmd("w", cmd_write, "write");
    cmd("wq", cmd_write_quit, "write+quit");
    cmd("bn", cmd_buffer_next, "next buf");
    cmd("bp", cmd_buffer_prev, "prev buf");
    cmd("ls", cmd_buffer_list, "list bufs");
    cmd("refresh", cmd_buf_refresh, "refresh contents");
    cmd("b", cmd_buffer_switch, "switch buf");
    cmd("bd", cmd_buffer_delete, "delete buf");
    cmd("e", cmd_edit, "edit file");
    cmd("c", cmd_cpick, "pick cmd");
    cmd("keybinds", cmd_list_keybinds, "list keybinds");
    cmd("echo", cmd_echo, "echo");
    cmd("history", cmd_history, "cmd hist");
    cmd("hfzf", cmd_history_fzf, "fuzzy search command history");
    cmd("jfzf", cmd_jumplist_fzf, "fuzzy search jump list");
    cmd("reg", cmd_registers, "registers");
    cmd("put", cmd_put, "put reg");
    cmd("undo", cmd_undo, "undo");
    cmd("redo", cmd_redo, "redo");
    cmd("repeat", cmd_repeat, "repeat last");
    cmd("record", cmd_macro_record, "record macro");
    cmd("play", cmd_macro_play, "play macro");
    cmd("ln", cmd_ln, "line nums");
    cmd("rln", cmd_rln, "relative numbers");
    cmd("copen", cmd_copen, "qf open");
    cmd("cclose", cmd_cclose, "qf close");
    cmd("ctoggle", cmd_ctoggle, "qf toggle");
    cmd("cadd", cmd_cadd, "qf add");
    cmd("cclear", cmd_cclear, "qf clear");
    cmd("cnext", cmd_cnext, "qf next");
    cmd("cprev", cmd_cprev, "qf prev");
    cmd("copenidx", cmd_copenidx, "qf open N");
    cmd("ssearch", cmd_ssearch, "search current file");
    cmd("rgword", cmd_rg_word, "ripgrep word under cursor");
    cmd("rg", cmd_rg, "ripgrep");
    cmd("tag", cmd_tag, "jump to tag definition");
    cmd("shq", cmd_shq, "shell cmd");
    cmd("sed", cmd_sed, "apply sed expression to buffer");
    cmd("cd", cmd_cd, "chdir");
    cmd("pwd", cmd_cd, "current dir");
    cmd("fzf", cmd_fzf, "pick a file(s)");
    cmd("recent", cmd_recent, "recent files");
    cmd("logclear", cmd_logclear, "clear .hedlog");
    cmd("shell", cmd_shell, "run shell cmd");
    cmd("git", cmd_git, "run lazygit");
    cmd("wrap", cmd_wrap, "toggle wrap");
    cmd("wrapdefault", cmd_wrapdefault, "toggle default wrap");
    cmd("new_line", cmd_new_line, "open new line below");
    cmd("new_line_above", cmd_new_line_above, "open new line above");
    cmd("split", cmd_split, "horizontal split");
    cmd("vsplit", cmd_vsplit, "vertical split");
    cmd("wfocus", cmd_wfocus, "focus next window");
    cmd("wclose", cmd_wclose, "close window");
    cmd("new", cmd_new, "new split with empty buffer");
    cmd("wh", cmd_wleft, "focus window left");
    cmd("wj", cmd_wdown, "focus window down");
    cmd("wk", cmd_wup, "focus window up");
    cmd("wl", cmd_wright, "focus window right");
    cmd("ts", cmd_ts, "ts on|off|auto");
    cmd("tslang", cmd_tslang, "tslang <name>");
    cmd("tsi", cmd_tsi, "install ts lang");
    cmd("reload", cmd_reload, "rebuild+restart hed");
    cmd("modal", cmd_modal_from_current, "convert current window to modal");
    cmd("unmodal", cmd_modal_to_layout, "convert modal back to normal window");
    cmd("foldnew", cmd_fold_new, "create fold region");
    cmd("foldrm", cmd_fold_rm, "remove fold at line");
    cmd("foldtoggle", cmd_fold_toggle, "toggle fold at line");
    cmd("foldmethod", cmd_foldmethod, "set fold method");
    cmd("foldupdate", cmd_foldupdate, "update folds");
    cmd("plugins",  cmd_plugins,  "list loaded plugins");
    cmd("goto",     cmd_goto,     "goto <line> | <motion> [count]");
    cmd("modeless", cmd_modeless, "modeless on|off|toggle");
}

static void register_hooks(void) {
    hook_register_mode(HOOK_MODE_CHANGE, hook_change_cursor_shape);
    undo_register_hooks();
}

static int core_init(void) {
    register_commands();
    register_hooks();
    return 0;
}

const Plugin plugin_core = {
    .name   = "core",
    .desc   = "default command set + editor-wide hooks",
    .init   = core_init,
    .deinit = NULL,
};
