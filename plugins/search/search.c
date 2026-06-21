/* search plugin: ripgrep-driven search across the project, the current
 * buffer, and arbitrary shell output. Owns:
 *
 *   :rg [pat]   - ripgrep the project (interactive fzf if no pattern)
 *   :rgword     - ripgrep word under cursor; also bound to `gr`
 *   :ssearch    - ripgrep the current file with live fzf
 *   :shq <cmd>  - run a shell command, parse file:line:col:text into qf
 *
 * Results land in the quickfix list (or jump directly when there's a
 * single match). cmd_rg / cmd_rg_word both pipe through cmd_shq, which
 * is why they live together.
 *
 * Sibling: the non-ripgrep pickers (cmd_cpick, cmd_fzf, cmd_recent)
 * live in plugins/pickers/. */

#include "hed.h"
/* Direct fzf access: ssearch uses fzf's --bind 'change:reload:...' for
 * live ripgrep — that's a fzf-specific feature the generic picker_list
 * abstraction can't express, so this plugin reaches across to pickers/
 * instead of going through input/picker.h. */
#include "pickers/fzf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void cmd_shq(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("Usage: :shq <command>");
        return;
    }
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "%s 2>/dev/null", args);

    char **lines = NULL;
    int    n     = 0;
    if (!term_cmd_capture(cmd, &lines, &n)) {
        ed_set_status_message("shq: failed to run");
        return;
    }

    qf_clear(&E.qf);
    int added = 0;
    for (int i = 0; i < n; i++) {
        /* Try parse as file:line:col:text (lines are newline-stripped). */
        char tmp[2048];
        snprintf(tmp, sizeof(tmp), "%s", lines[i]);
        char *file; int lno, col; const char *text;
        if (qf_parse_grep_line(tmp, &file, &lno, &col, &text)) {
            qf_add(&E.qf, file, lno, col, text);
        } else {
            /* Plain line -> text-only quickfix item */
            qf_add(&E.qf, NULL, 0, 0, lines[i]);
        }
        added++;
    }
    term_cmd_free(lines, n);

    if (added > 0) {
        qf_open(&E.qf, E.qf.height > 0 ? E.qf.height : 8);
        ed_set_status_message("shq: %d line(s)", added);
    } else {
        ed_set_status_message("shq: no output");
    }
}

static void cmd_ssearch(const char *args) {
    (void)args;
    BUF(buf)
    EdError err = buf_save_in(buf);
    if (err != ED_OK) {
        ed_set_status_message("Warning: save failed: %s", ed_error_string(err));
    }

    char esc_query[8];
    snprintf(esc_query, sizeof(esc_query), "''");
    const char *rg_base =
        "rg --vimgrep --no-heading --color=never -n --column --";
    char esc_file[1024];
    shell_escape_single(buf->filename, esc_file, sizeof(esc_file));

    char fzf_opts[4096];
    snprintf(fzf_opts, sizeof(fzf_opts),
             "--ansi --phony --query %s "
             "--bind 'change:reload:%s {q} %s 2>/dev/null || true' "
             "--bind 'alt-a:select-all,alt-d:deselect-all,alt-t:toggle-all' "
             "--delimiter ':' --with-nth 4..",
             esc_query, rg_base, esc_file);

    char **sel = NULL;
    int cnt = 0;
    if (!fzf_run_opts("printf ''", fzf_opts, 1, &sel, &cnt) || cnt <= 0) {
        fzf_free(sel, cnt);
        ed_set_status_message("ssearch: no selection");
        return;
    }

    if (cnt == 1) {
        /* Single selection: jump directly in current buffer */
        char tmp[1024];
        snprintf(tmp, sizeof(tmp), "%s", sel[0]);
        fzf_free(sel, cnt);

        int lno, col;
        if (!qf_parse_grep_line(tmp, NULL, &lno, &col, NULL)) {
            ed_set_status_message("ssearch: invalid");
            return;
        }

        Window *win = window_cur();
        if (!win)
            return;
        if (lno < 1)
            lno = 1;
        if (lno > buf->num_rows)
            lno = buf->num_rows;
        win->cursor.y = lno - 1;
        win->cursor.x = 0;
        (void)col;
        buf_center_screen();
        ed_set_status_message("ssearch: line %d", lno);
        return;
    }

    /* Multiple selections: populate quickfix with all matches */
    qf_clear(&E.qf);
    for (int i = 0; i < cnt; i++) {
        char tmp[1024];
        snprintf(tmp, sizeof(tmp), "%s", sel[i]);

        int lno, col; const char *text;
        if (!qf_parse_grep_line(tmp, NULL, &lno, &col, &text))
            continue;
        /* file field is always the current file here */
        qf_add(&E.qf, buf->filename, lno, col, text);
    }
    fzf_free(sel, cnt);

    if (arrlen(E.qf.items) > 0) {
        qf_open(&E.qf, E.qf.height > 0 ? E.qf.height : 8);
        ed_set_status_message("ssearch: %td item(s)", arrlen(E.qf.items));
    } else {
        ed_set_status_message("ssearch: no parsed selections");
    }
}

static void cmd_rg(const char *args) {
    /* If a pattern is provided, run rg once and populate quickfix directly (no
     * fzf). */
    if (args && *args) {
        char pattern[512];
        size_t pn = str_trim_whitespace(args, pattern, sizeof(pattern));
        if (pn > 0 && pattern[0]) {
            char esc_pat[1024];
            shell_escape_single(pattern, esc_pat, sizeof(esc_pat));

            char cmd[1600];
            snprintf(
                cmd, sizeof(cmd),
                "rg --vimgrep --no-heading --color=never -n --column -- %s",
                esc_pat);
            cmd_shq(cmd);
            return;
        }
    }

    /* No pattern: interactive fzf-based ripgrep search. */
    char esc_query[512];
    snprintf(esc_query, sizeof(esc_query), "''");

    const char *rg_base =
        "rg --vimgrep --no-heading --color=never -n --column --";
    char fzf_opts[2048];
    snprintf(fzf_opts, sizeof(fzf_opts),
             "--ansi --phony --query %s "
             "--bind 'change:reload:%s {q} 2>/dev/null || true' "
             "--bind 'alt-a:select-all,alt-d:deselect-all,alt-t:toggle-all' "
             "--delimiter ':' --with-nth 4.. "
             "--preview 'printf \"%%s:%%s\\n\\n\" {1} {2}; "
             "command -v bat >/dev/null 2>&1 && "
             "bat --style=plain --color=always --highlight-line {2} {1} "
             "|| sed -n \"1,200p\" {1} 2>/dev/null' "
             "--preview-window right,60%%,wrap,+{2}",
             esc_query, rg_base);

    char **sel = NULL;
    int cnt = 0;
    if (!fzf_run_opts("printf ''", fzf_opts, 1, &sel, &cnt)) {
        ed_set_status_message("fzf not available or failed to run");
        return;
    }
    if (cnt <= 0) {
        fzf_free(sel, cnt);
        ed_set_status_message("rg: no selection");
        return;
    }

    if (cnt == 1) {
        /* Single selection: jump directly and skip quickfix */
        char tmp[1024];
        snprintf(tmp, sizeof(tmp), "%s", sel[0]);
        fzf_free(sel, cnt);

        char *file; int lno, col; const char *text;
        if (!qf_parse_grep_line(tmp, &file, &lno, &col, &text)) {
            ed_set_status_message("rg: invalid selection");
            return;
        }

        qf_clear(&E.qf);
        E.qf.sel = 0;
        E.qf.scroll = 0;
        qf_add(&E.qf, file, lno, col, text);
        qf_open_selected(&E.qf);
        ed_set_status_message("rg: opened %s:%d:%d", file, lno, col);
        return;
    }

    /* Multiple selections: fill quickfix list */
    qf_clear(&E.qf);
    for (int i = 0; i < cnt; i++) {
        char tmp[1024];
        snprintf(tmp, sizeof(tmp), "%s", sel[i]);
        char *file; int lno, col; const char *text;
        if (!qf_parse_grep_line(tmp, &file, &lno, &col, &text))
            continue;
        qf_add(&E.qf, file, lno, col, text);
    }
    fzf_free(sel, cnt);

    if (arrlen(E.qf.items) > 0) {
        qf_open(&E.qf, E.qf.height > 0 ? E.qf.height : 8);
        ed_set_status_message("rg: %td items", arrlen(E.qf.items));
    } else {
        ed_set_status_message("rg: no parsed selections");
    }
}

static void cmd_rg_word(const char *args) {
    (void)args;
    /* Get word under cursor */
    StrBuf w = strbuf_new();
    if (!buf_get_word_under_cursor(&w) || w.len == 0) {
        strbuf_free(&w);
        ed_set_status_message("rgword: no word under cursor");
        return;
    }

    char pattern[512];
    size_t n = w.len;
    if (n >= sizeof(pattern))
        n = sizeof(pattern) - 1;
    memcpy(pattern, w.data, n);
    pattern[n] = '\0';
    strbuf_free(&w);

    char esc_pat[1024];
    shell_escape_single(pattern, esc_pat, sizeof(esc_pat));

    /* Use cmd_shq pipeline to run rg and populate quickfix. */
    char cmd[1600];
    snprintf(cmd, sizeof(cmd),
             "rg --vimgrep --no-heading --color=never -n --column -- %s",
             esc_pat);
    cmd_shq(cmd);
}

static int search_init(void) {
    cmd("rg",      cmd_rg,      "ripgrep");
    cmd("rgword",  cmd_rg_word, "ripgrep word under cursor");
    cmd("ssearch", cmd_ssearch, "search current file");
    cmd("shq",     cmd_shq,     "shell cmd");
    return 0;
}

const Plugin plugin_search = {
    .name   = "search",
    .desc   = "ripgrep-driven search (:rg, :rgword, :ssearch, :shq)",
    .init   = search_init,
    .deinit = NULL,
};
