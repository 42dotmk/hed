/* treesitter plugin: dynamic-grammar syntax highlighting.
 *
 * Implementation lives next to this file in ts_impl.c. This plugin
 * owns activation: the :ts / :tslang / :tsi commands. Highlighting
 * itself is invoked from core via weak references to ts_is_enabled /
 * ts_buffer_autoload / ts_buffer_reparse — when this plugin is built
 * those refs resolve to the real implementation; otherwise they're
 * NULL and core skips the calls. */

#include "hed.h"
#include "ts.h"
#include "theme.h"
#include "shell/shell.h"
#include "utils/fzf.h"

static void cmd_ts(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("ts: %s", ts_is_enabled() ? "on" : "off");
        return;
    }
    if (strcmp(args, "on") == 0) {
        ts_set_enabled(1);
        for (int i = 0; i < (int)arrlen(E.buffers); i++) {
            ts_buffer_autoload(&E.buffers[i]);
            ts_buffer_reparse(&E.buffers[i]);
        }
        ed_set_status_message("ts: on");
    } else if (strcmp(args, "off") == 0) {
        ts_set_enabled(0);
        ed_set_status_message("ts: off");
    } else if (strcmp(args, "auto") == 0) {
        ts_set_enabled(1);
        Buffer *b = buf_cur();
        if (b) {
            if (!ts_buffer_autoload(b))
                ed_set_status_message("ts: no lang for current file");
            ts_buffer_reparse(b);
        }
        ed_set_status_message("ts: auto");
    } else {
        ed_set_status_message("ts: on|off|auto");
    }
}

static void cmd_tslang(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("tslang: <name>");
        return;
    }
    Buffer *b = buf_cur();
    if (!b) return;
    ts_set_enabled(1);
    if (!ts_buffer_load_language(b, args)) {
        ed_set_status_message("tslang: failed for %s", args);
        return;
    }
    ts_buffer_reparse(b);
    ed_set_status_message("tslang: %s", args);
}

static void cmd_tsi(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("tsi: <lang>");
        return;
    }
    char cmd_str[256];
    snprintf(cmd_str, sizeof(cmd_str), "tsi %s", args);
    cmd_shell(cmd_str);
}

static void cmd_theme(const char *args) {
    if (args && *args) {
        if (theme_activate(args) != 0) {
            ed_set_status_message("theme: unknown '%s'", args);
            return;
        }
        ed_set_status_message("theme: %s", args);
        return;
    }

    /* No args → fzf picker over the registered themes. */
    const char *const *names = theme_list();
    if (!names || !names[0]) {
        const char *active = theme_active_name();
        ed_set_status_message("theme: %s (no themes registered)",
                              active ? active : "default");
        return;
    }
    int n = 0;
    while (names[n])
        n++;

    char **sel = NULL;
    int    cnt = 0;
    /* fzf_pick_list takes const char **; theme_list returns const char *const *.
     * The cast is safe — fzf_pick_list never writes through the array. */
    if (!fzf_pick_list((const char **)names, n, 0, &sel, &cnt) || cnt <= 0 ||
        !sel[0] || !sel[0][0]) {
        fzf_free(sel, cnt);
        return;
    }
    if (theme_activate(sel[0]) != 0)
        ed_set_status_message("theme: unknown '%s'", sel[0]);
    else
        ed_set_status_message("theme: %s", sel[0]);
    fzf_free(sel, cnt);
}

static int treesitter_init(void) {
    ts_seed_default_theme();
    cmd("ts",     cmd_ts,     "ts on|off|auto");
    cmd("tslang", cmd_tslang, "tslang <name>");
    cmd("tsi",    cmd_tsi,    "install ts lang");
    cmd("theme",  cmd_theme,  "theme [name]");
    /* Phase-1 renderer abstraction: tree-sitter is the first
     * highlighter to emit AttrSpans instead of bytes. Registers for
     * every filetype — ts_render_pre_hook itself no-ops when the
     * buffer has no parsed tree. */
    hook_register_render(HOOK_RENDER_PRE, -1, "*",
                         (HookRenderCallback)ts_render_pre_hook);
    /* Own the per-buffer lifecycle entirely from the plugin side —
     * core no longer carries any tree-sitter-shaped slots or calls. */
    hook_register_buffer(HOOK_BUFFER_OPEN,  -1, "*", ts_on_buffer_open);
    hook_register_buffer(HOOK_BUFFER_CLOSE, -1, "*", ts_on_buffer_close);
    /* Existing buffers (the placeholder buf_new at startup) get the
     * same treatment as buffers opened later. */
    for (int i = 0; i < (int)arrlen(E.buffers); i++) {
        HookBufferEvent ev = {.buf = &E.buffers[i],
                              .filename = E.buffers[i].filename};
        ts_on_buffer_open(&ev);
    }
    return 0;
}

const Plugin plugin_treesitter = {
    .name   = "treesitter",
    .desc   = "tree-sitter syntax highlighting",
    .init   = treesitter_init,
    .deinit = NULL,
};
