#include "cmd_search.h"
#include "cmd_util.h"
#include "../hed.h"
#include "fzf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void cmd_cpick(const char *args) {
    (void)args;
    /* Build printf list with name\tdesc lines for fzf preview */
    char pipebuf[8192];
    size_t off = 0;
    off += snprintf(pipebuf + off, sizeof(pipebuf) - off, "printf '%%s\t%%s\\n' ");

    for (int i = 0; i < command_count; i++) {
        const char *nm = commands[i].name ? commands[i].name : "";
        const char *ds = commands[i].desc ? commands[i].desc : "";
        char en[256], ed[512];
        shell_escape_single(nm, en, sizeof(en));
        shell_escape_single(ds, ed, sizeof(ed));
        size_t need = strlen(en) + 1 + strlen(ed) + 1;
        if (off + need + 4 >= sizeof(pipebuf)) break;
        memcpy(pipebuf + off, en, strlen(en)); off += strlen(en);
        pipebuf[off++] = ' ';
        memcpy(pipebuf + off, ed, strlen(ed)); off += strlen(ed);
        pipebuf[off++] = ' ';
    }
    pipebuf[off] = '\0';

    const char *fzf_opts = "--delimiter '\t' --with-nth 1 --preview 'echo {2}' --preview-window right,60%,wrap";
    char **sel = NULL;
    int cnt = 0;
    if (!fzf_run_opts(pipebuf, fzf_opts, 0, &sel, &cnt) || cnt == 0) {
        ed_set_status_message("c: canceled");
        fzf_free(sel, cnt);
        return;
    }

    /* Parse the picked line: name<TAB>desc */
    char *picked = sel[0];
    char *tab = strchr(picked, '\t');
    if (tab) *tab = '\0';

    /* Pre-fill command line and stay in command mode */
    ed_set_mode(MODE_COMMAND);
    E.command_len = 0;
    size_t ll = strlen(picked);
    size_t maxcopy = sizeof(E.command_buf) - 2;
    if (ll > maxcopy) ll = maxcopy;
    memcpy(E.command_buf, picked, ll);
    E.command_len = (int)ll;
    E.command_buf[E.command_len++] = ' ';
    E.command_buf[E.command_len] = '\0';
    ed_set_status_message(":%s", E.command_buf);
    E.stay_in_command = 1;
    fzf_free(sel, cnt);
}

void cmd_ssearch(const char *args) {
    (void)args;
    Buffer *buf = buf_cur();
    if (!buf) {
        ed_set_status_message("ssearch: no buffer");
        return;
    }
    if (!buf->filename || !*buf->filename) {
        ed_set_status_message("ssearch: file has no name");
        return;
    }

    /* Save file to ensure on-disk content matches buffer */
    EdError err = buf_save_in(buf);
    if (err != ED_OK) {
        ed_set_status_message("Warning: save failed: %s", ed_error_string(err));
        /* Continue anyway - user might want to search unsaved content */
    }

    /* Build fzf options with ripgrep reload bound to query changes */
    char esc_query[8];
    snprintf(esc_query, sizeof(esc_query), "''");
    const char *rg_base = "rg --vimgrep --no-heading --color=never -n --column --";
    char esc_file[1024];
    shell_escape_single(buf->filename, esc_file, sizeof(esc_file));

    char fzf_opts[4096];
    snprintf(fzf_opts, sizeof(fzf_opts),
        "--ansi --phony --query %s "
        "--bind \"change:reload:%s {q} %s 2>/dev/null || true\" "
        "--delimiter ':' --with-nth 4.. "
        "--preview 'command -v bat >/dev/null 2>&1 && bat --style=plain --color=always --line-range :200 {1} || sed -n \"1,200p\" {1} 2>/dev/null' "
        "--preview-window right,60%%,wrap",
        esc_query, rg_base, esc_file);

    char **sel = NULL;
    int cnt = 0;
    if (!fzf_run_opts("printf ''", fzf_opts, 0, &sel, &cnt) || cnt <= 0) {
        fzf_free(sel, cnt);
        ed_set_status_message("ssearch: no selection");
        return;
    }

    /* Parse first selection: file:line:col:text */
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", sel[0]);
    fzf_free(sel, cnt);

    char *p1 = strchr(tmp, ':');
    if (!p1) {
        ed_set_status_message("ssearch: invalid");
        return;
    }
    *p1 = '\0'; /* file (unused, current file) */

    char *p2 = strchr(p1 + 1, ':');
    if (!p2) {
        ed_set_status_message("ssearch: invalid");
        return;
    }
    *p2 = '\0';
    int lno = atoi(p1 + 1);

    char *p3 = strchr(p2 + 1, ':');
    int col = 1;
    if (p3) {
        *p3 = '\0';
        col = atoi(p2 + 1);
    }

    Window *win = window_cur();
    if (!win) return;
    if (lno < 1) lno = 1;
    if (lno > buf->num_rows) lno = buf->num_rows;
    win->cursor.y = lno - 1;
    win->cursor.x = 0;
    (void)col;
    buf_center_screen();
    ed_set_status_message("ssearch: line %d", lno);
}

void cmd_rg(const char *args) {
    /* Build command: rg in current dir with vimgrep-like output */
    char esc_query[512];
    esc_query[0] = '\0';
    if (args && *args) {
        shell_escape_single(args, esc_query, sizeof(esc_query));
    } else {
        snprintf(esc_query, sizeof(esc_query), "''");
    }

    const char *rg_base = "rg --vimgrep --no-heading --color=never -n --column --";
    char fzf_opts[2048];
    snprintf(fzf_opts, sizeof(fzf_opts),
        "--ansi --phony --query %s "
        "--bind 'change:reload:%s {q} 2>/dev/null || true' "
        "--delimiter ':' --with-nth 4.. "
        "--preview 'command -v bat >/dev/null 2>&1 && bat --style=plain --color=always --line-range :200 {1} || sed -n \"1,200p\" {1} 2>/dev/null' "
        "--preview-window right,60%%,wrap",
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

    /* Parse selections of the form: file:line:col:text */
    if (cnt == 1) {
        char *line = sel[0];
        char tmp[1024];
        snprintf(tmp, sizeof(tmp), "%s", line);
        char *p1 = strchr(tmp, ':');
        if (!p1) {
            fzf_free(sel, cnt);
            ed_set_status_message("rg: invalid selection");
            return;
        }
        *p1 = '\0';
        char *file = tmp;
        char *p2 = strchr(p1 + 1, ':');
        if (!p2) {
            fzf_free(sel, cnt);
            ed_set_status_message("rg: invalid selection");
            return;
        }
        *p2 = '\0';
        int lno = atoi(p1 + 1);
        char *p3 = strchr(p2 + 1, ':');
        if (!p3) {
            fzf_free(sel, cnt);
            ed_set_status_message("rg: invalid selection");
            return;
        }
        *p3 = '\0';
        int col = atoi(p2 + 1);
        /* Jump directly without opening quickfix window */
        qf_clear(&E.qf);
        E.qf.sel = 0;
        E.qf.scroll = 0;
        qf_add(&E.qf, file, lno, col, p3 + 1);
        qf_open_selected(&E.qf);
        ed_set_status_message("rg: opened %s:%d:%d", file, lno, col);
    } else {
        qf_clear(&E.qf);
        for (int i = 0; i < cnt; i++) {
            char tmp[1024];
            snprintf(tmp, sizeof(tmp), "%s", sel[i]);
            char *p1 = strchr(tmp, ':');
            if (!p1) continue;
            *p1 = '\0';
            char *file = tmp;
            char *p2 = strchr(p1 + 1, ':');
            if (!p2) continue;
            *p2 = '\0';
            int lno = atoi(p1 + 1);
            char *p3 = strchr(p2 + 1, ':');
            if (!p3) continue;
            *p3 = '\0';
            int col = atoi(p2 + 1);
            qf_add(&E.qf, file, lno, col, p3 + 1);
        }
        if (E.qf.len > 0) {
            qf_open(&E.qf, E.qf.height > 0 ? E.qf.height : 8);
            ed_set_status_message("rg: %d items", E.qf.len);
        } else {
            ed_set_status_message("rg: no parsed selections");
        }
    }
    fzf_free(sel, cnt);
}

void cmd_fzf(const char *args) {
    (void)args;
    qf_clear(&E.qf);
    char **sel = NULL;
    int cnt = 0;
    const char *find_files_cmd = "(command -v rg >/dev/null 2>&1 && rg --files || find . -type f -print)";
    const char *fzf_opts = "--preview 'command -v bat >/dev/null 2>&1 && bat --style=plain --color=always --line-range :200 {} || sed -n \"1,200p\" {} 2>/dev/null' --preview-window right,60%,wrap";
    if (fzf_run_opts(find_files_cmd, fzf_opts, 0, &sel, &cnt) && cnt > 0 && sel[0] && sel[0][0]) {
        buf_open_or_switch(sel[0]);
    } else {
        ed_set_status_message("fzf: no selection");
    }
    fzf_free(sel, cnt);
}

void cmd_recent(const char *args) {
    (void)args;

    int len = recent_files_len(&E.recent_files);
    if (len == 0) {
        ed_set_status_message("No recent files");
        return;
    }

    char cmd[8192];
    int off = 0;

    off += snprintf(cmd + off, sizeof(cmd) - off, "printf '%%s\\n' ");

    for (int i = 0; i < len && off < (int)sizeof(cmd) - 1024; i++) {
        const char *file = recent_files_get(&E.recent_files, i);
        if (!file) continue;

        char escaped[1024];
        shell_escape_single(file, escaped, sizeof(escaped));

        size_t need = strlen(escaped) + 2;
        if (off + need >= sizeof(cmd)) {
            break;
        }

        off += snprintf(cmd + off, sizeof(cmd) - off, "%s ", escaped);
    }

    cmd[off] = '\0';

    char **sel = NULL;
    int cnt = 0;
    const char *fzf_opts = "--preview 'command -v bat >/dev/null 2>&1 && bat --style=plain --color=always --line-range :200 {} || sed -n \"1,200p\" {} 2>/dev/null' --preview-window right,60%,wrap";

    if (fzf_run_opts(cmd, fzf_opts, 0, &sel, &cnt) && cnt > 0 && sel[0] && sel[0][0]) {
        buf_open_or_switch(sel[0]);
    } else {
        ed_set_status_message("no selection");
    }

    fzf_free(sel, cnt);
}

void cmd_shq(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("Usage: :shq <command>");
        return;
    }
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "%s 2>/dev/null", args);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        ed_set_status_message("shq: failed to run");
        return;
    }

    qf_clear(&E.qf);
    char line[2048];
    int added = 0;
    while (fgets(line, sizeof(line), fp)) {
        /* Try parse as file:line:col:text */
        char tmp[2048];
        snprintf(tmp, sizeof(tmp), "%s", line);
        char *p1 = strchr(tmp, ':');
        if (p1) {
            *p1 = '\0';
            char *file = tmp;
            char *p2 = strchr(p1 + 1, ':');
            if (p2) {
                *p2 = '\0';
                int lno = atoi(p1 + 1);
                char *p3 = strchr(p2 + 1, ':');
                int col = 1;
                char *text = NULL;
                if (p3) {
                    *p3 = '\0';
                    col = atoi(p2 + 1);
                    text = p3 + 1;
                } else {
                    col = atoi(p2 + 1);
                    text = "";
                }
                size_t tl = strlen(text);
                if (tl && (text[tl - 1] == '\n' || text[tl - 1] == '\r'))
                    text[--tl] = '\0';
                qf_add(&E.qf, file, lno, col, text);
                added++;
                continue;
            }
        }
        /* Plain line -> text-only quickfix item */
        size_t ll = strlen(line);
        if (ll && (line[ll - 1] == '\n' || line[ll - 1] == '\r'))
            line[--ll] = '\0';
        qf_add(&E.qf, NULL, 0, 0, line);
        added++;
    }
    pclose(fp);

    if (added > 0) {
        qf_open(&E.qf, E.qf.height > 0 ? E.qf.height : 8);
        ed_set_status_message("shq: %d line(s)", added);
    } else {
        ed_set_status_message("shq: no output");
    }
}
