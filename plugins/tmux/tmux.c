/* tmux plugin: runner pane integration. Implementation lives in
 * tmux_impl.c next to this file. The plugin owns the activation
 * surface — commands, the send-line keybind, and a colon-prompt
 * history hook that splices runner history into Up/Down arrow
 * navigation when the user is editing a `tmux_send ...` command. */

#include "hed.h"
#include "tmux.h"
#include "utils/fzf.h"
#include "utils/yank.h"
#include "keybinds_builtins.h"
#include "command_mode.h"
#include "prompt.h"
#include <string.h>

static void cmd_tmux_toggle(const char *args) {
    (void)args;
    tmux_toggle_pane();
}

static void cmd_tmux_send(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("Usage: :tmux_send <command>");
        return;
    }
    tmux_send_command(args);
}

static void cmd_tmux_kill(const char *args) {
    (void)args;
    tmux_kill_pane();
}

static void cmd_tmux_focus(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("Usage: :tmux_focus <name>");
        return;
    }
    tmux_pane_focus(args);
}

static void cmd_tmux_attach(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("Usage: :tmux_attach <name>");
        return;
    }
    char **sel = NULL;
    int cnt = 0;
    const char *list_cmd =
        "tmux list-panes -a -F "
        "'#{pane_id} #{session_name}:#{window_name}.#{pane_index} "
        "#{pane_current_command} | #{pane_title}'";
    if (!fzf_run(list_cmd, 0, &sel, &cnt) || cnt <= 0 || !sel[0] || !sel[0][0]) {
        fzf_free(sel, cnt);
        return;
    }
    /* First whitespace-separated field of the picked line is the pane id. */
    char pane_id[64] = {0};
    if (sscanf(sel[0], "%63s", pane_id) != 1 || !pane_id[0]) {
        ed_set_status_message("tmux: could not parse pane id");
        fzf_free(sel, cnt);
        return;
    }
    tmux_pane_attach(args, pane_id);
    fzf_free(sel, cnt);
}

#define TMUX_PANE_LIST_MAX 16

static void cmd_tmux_panes(const char *args) {
    (void)args;
    const char *names[TMUX_PANE_LIST_MAX];
    int n = tmux_pane_list(names, TMUX_PANE_LIST_MAX);
    if (n <= 0) {
        ed_set_status_message("tmux: no registered panes");
        return;
    }

    char **sel = NULL;
    int cnt = 0;
    if (!fzf_pick_list(names, n, 0, &sel, &cnt) || cnt <= 0 || !sel[0] ||
        !sel[0][0]) {
        fzf_free(sel, cnt);
        return;
    }
    tmux_pane_focus(sel[0]);
    fzf_free(sel, cnt);
}

/* Send the paragraph under cursor to whichever pane was last focused.
 * Exposed both as a keybind callback and as the `:tmux_send_line`
 * command. Defaults to the runner pane until something else is focused. */
static void kb_tmux_send_line(void) {
    SizedStr para = sstr_new();
    if (!buf_get_paragraph_under_cursor(&para) || para.len == 0) {
        sstr_free(&para);
        ed_set_status_message("tmux: no paragraph to send");
        return;
    }

    char *cmd_str = malloc(para.len + 1);
    if (!cmd_str) {
        sstr_free(&para);
        ed_set_status_message("tmux: out of memory");
        return;
    }

    memcpy(cmd_str, para.data, para.len);
    cmd_str[para.len] = '\0';
    tmux_pane_send_focused(cmd_str);

    free(cmd_str);
    sstr_free(&para);
}

static void cmd_tmux_send_line(const char *args) {
    (void)args;
    kb_tmux_send_line();
}

/* Send the current visual selection to whichever pane was last focused.
 * Joins block/line/char selections into a single newline-separated string,
 * matching how :tmux_send delivers a multi-line command. */
static void kb_tmux_send_selection(void) {
    BUFWIN(buf, win);
    if (!buf || !win || win->sel.type == SEL_NONE) {
        ed_set_status_message("tmux: no selection");
        return;
    }

    int block_mode = (E.mode == MODE_VISUAL_BLOCK);
    TextSelection sel;
    if (!kb_visual_to_textsel(buf, win, block_mode, &sel)) {
        ed_set_status_message("tmux: no selection");
        return;
    }

    YankData yd = yank_data_new(buf, &sel);
    if (yd.num_rows <= 0 || !yd.rows) {
        yank_data_free(&yd);
        ed_set_status_message("tmux: empty selection");
        return;
    }

    SizedStr joined = sstr_new();
    for (int i = 0; i < yd.num_rows; i++) {
        sstr_append(&joined, yd.rows[i].data, yd.rows[i].len);
        if (i < yd.num_rows - 1)
            sstr_append_char(&joined, '\n');
    }
    yank_data_free(&yd);

    if (joined.len == 0) {
        sstr_free(&joined);
        ed_set_status_message("tmux: empty selection");
        return;
    }

    char *cmd_str = sstr_to_cstr(&joined);
    sstr_free(&joined);
    if (!cmd_str) {
        ed_set_status_message("tmux: out of memory");
        return;
    }

    tmux_pane_send_focused(cmd_str);
    free(cmd_str);

    kb_visual_clear(win);
    ed_set_mode(MODE_NORMAL);
}

static void cmd_tmux_send_selection(const char *args) {
    (void)args;
    kb_tmux_send_selection();
}

/* Colon-prompt history hook. Only handles the case where the user is
 * editing a "tmux_send ..." line; otherwise returns 0 so the next hook
 * (or the default command-history fallback) takes over. */
static int tmux_send_history_hook(Prompt *p, int dir, void *ud) {
    (void)ud;
    static const char prefix[] = "tmux_send";
    const size_t plen = sizeof(prefix) - 1;
    if ((size_t)p->len < plen) return 0;
    if (memcmp(p->buf, prefix, plen) != 0) return 0;
    if ((size_t)p->len > plen && p->buf[plen] != ' ') return 0;

    const char *args = p->buf + plen;
    if (*args == ' ') args++;
    int args_len = p->len - (int)(args - p->buf);

    char candidate[512];
    int ok =
        (dir < 0)
            ? tmux_history_browse_up(args, args_len, candidate, (int)sizeof(candidate))
            : tmux_history_browse_down(candidate, (int)sizeof(candidate), NULL);
    if (!ok) return 0;

    char line[PROMPT_BUF_CAP];
    int n = snprintf(line, sizeof(line), "tmux_send%s%s",
                     candidate[0] ? " " : "", candidate);
    if (n < 0) n = 0;
    if (n >= (int)sizeof(line)) n = (int)sizeof(line) - 1;
    prompt_set_text(p, line, n);
    return 1;
}

static int tmux_plugin_init(void) {
    /* Default "runner" pane: empty spawn cmd = user's login shell, split
     * below the editor. Other plugins can register their own panes via
     * tmux_pane_register(). */
    tmux_pane_register("runner", NULL, TMUX_SPLIT_BELOW);

    cmd("tmux_toggle",    cmd_tmux_toggle,    "tmux toggle runner pane");
    cmd("tmux_send",      cmd_tmux_send,      "tmux send command");
    cmd("tmux_kill",      cmd_tmux_kill,      "tmux kill runner pane");
    cmd("tmux_send_line",      cmd_tmux_send_line,      "tmux send paragraph to last focused pane");
    cmd("tmux_send_selection", cmd_tmux_send_selection, "tmux send visual selection to last focused pane");
    cmd("tmux_focus",          cmd_tmux_focus,          "tmux focus pane by name");
    cmd("tmux_panes",          cmd_tmux_panes,          "tmux fzf-pick a registered pane");
    cmd("tmux_attach",         cmd_tmux_attach,         "tmux bind a live pane to a name");
    mapn(" ts", kb_tmux_send_line,      "send paragraph to last focused tmux pane");
    mapv(" ts", kb_tmux_send_selection, "send selection to last focused tmux pane");

    cmd_prompt_history_register(tmux_send_history_hook, NULL);
    return 0;
}

const Plugin plugin_tmux = {
    .name   = "tmux",
    .desc   = "tmux runner pane integration",
    .init   = tmux_plugin_init,
    .deinit = NULL,
};
