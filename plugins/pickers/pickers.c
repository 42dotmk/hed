/* pickers plugin: fzf-driven pickers — files, recent files, command
 * palette, command history, jump list, buffer/keybind/plugin lists.
 * Pure UI on top of editor state (commands array, utils/history.c,
 * utils/recent_files.c, utils/jump_list.c); no core editing
 * primitives live here.
 *
 * Registration: pickers_init wires every implementation into
 * src/picker.h's name-keyed registry. Core commands like :b,
 * :commands, :keybinds, :plugins and the gF keybind invoke pickers
 * via picker_invoke and fall back gracefully when this plugin
 * isn't loaded. */

#include "hed.h"
#include "command_mode.h"
#include "picker.h"
#include "prompt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void cmd_history_fzf(const char *args) {
    (void)args;
    int hlen = hist_len(&E.history);
    if (hlen == 0) {
        ed_set_status_message("No history");
        return;
    }

    char tmppath[PATH_MAX];
    if (fs_temp_path("hed_hist", tmppath, sizeof(tmppath)) != ED_OK) {
        ed_set_status_message("hfzf: failed to reserve temp file");
        return;
    }
    FILE *fp = fopen(tmppath, "w");
    if (!fp) {
        fs_unlink(tmppath);
        ed_set_status_message("hfzf: failed to write temp file");
        return;
    }
    for (int i = 0; i < hlen; i++) {
        const char *line = hist_get(&E.history, i);
        if (line) fprintf(fp, "%s\n", line);
    }
    fclose(fp);

    char fzf_cmd[PATH_MAX + 8];
    snprintf(fzf_cmd, sizeof(fzf_cmd), "cat %s", tmppath);

    char **sel = NULL;
    int cnt    = 0;
    fzf_run(fzf_cmd, 0, &sel, &cnt);
    fs_unlink(tmppath);

    if (cnt == 0 || !sel || !sel[0]) {
        fzf_free(sel, cnt);
        return;
    }

    /* Prefill the active : prompt with the picked history line and
     * keep it open for further editing. */
    Prompt *p = prompt_current();
    if (p) {
        prompt_set_text(p, sel[0], (int)strlen(sel[0]));
        ed_set_status_message(":%s", p->buf);
        prompt_keep_open();
    }
    fzf_free(sel, cnt);
}

static void cmd_jumplist_fzf(const char *args) {
    (void)args;
    int jlen = (int)arrlen(E.jump_list.entries);
    if (jlen == 0) {
        ed_set_status_message("Jump list is empty");
        return;
    }

    char tmppath[PATH_MAX];
    if (fs_temp_path("hed_jumps", tmppath, sizeof(tmppath)) != ED_OK) {
        ed_set_status_message("jfzf: failed to reserve temp file");
        return;
    }
    FILE *fp = fopen(tmppath, "w");
    if (!fp) {
        fs_unlink(tmppath);
        ed_set_status_message("jfzf: failed to write temp file");
        return;
    }
    /* Most recent first */
    for (int i = jlen - 1; i >= 0; i--) {
        JumpEntry *e = &E.jump_list.entries[i];
        if (e->filepath)
            fprintf(fp, "%s:%d:%d\n", e->filepath, e->cursor_y + 1, e->cursor_x + 1);
    }
    fclose(fp);

    char fzf_cmd[PATH_MAX + 8];
    snprintf(fzf_cmd, sizeof(fzf_cmd), "cat %s", tmppath);

    const char *fzf_opts =
        "--delimiter ':' "
        "--preview 'command -v bat >/dev/null 2>&1 "
            "&& bat --style=plain --color=always --highlight-line {2} "
                "--line-range {2}:+30 {1} "
            "|| awk \"NR>={2}-5 && NR<={2}+25\" {1}' "
        "--preview-window 'right,60%,+{2}-5'";

    char **sel = NULL;
    int cnt    = 0;
    fzf_run_opts(fzf_cmd, fzf_opts, 0, &sel, &cnt);
    fs_unlink(tmppath);

    if (cnt == 0 || !sel || !sel[0]) {
        fzf_free(sel, cnt);
        return;
    }

    /* Parse "filepath:line:col" by walking colons from the right. */
    char *entry = sel[0];
    char *last_colon = strrchr(entry, ':');
    if (!last_colon) { fzf_free(sel, cnt); return; }
    int col = atoi(last_colon + 1) - 1;
    *last_colon = '\0';

    char *prev_colon = strrchr(entry, ':');
    if (!prev_colon) { fzf_free(sel, cnt); return; }
    int line = atoi(prev_colon + 1) - 1;
    *prev_colon = '\0';

    buf_open_or_switch(entry, false);
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (buf) {
        int row = (line < buf->num_rows) ? line : buf->num_rows - 1;
        if (row < 0) row = 0;
        buf->cursor->y = row;
        buf->cursor->x = col;
        if (win) { win->cursor.y = row; win->cursor.x = col; }
    }
    fzf_free(sel, cnt);
}

/* Command palette: list every registered :command and prefill the
 * prompt with the picked name. Also serves as the second-Tab fzf
 * escalation in the colon prompt — registered with command_mode.c via
 * cmd_prompt_completion_picker_register. */
static void cmd_cpick(const char *args) {
    /* Build printf list with name\tdesc lines for fzf preview */
    char pipebuf[8192];
    size_t off = 0;
    off +=
        snprintf(pipebuf + off, sizeof(pipebuf) - off, "printf '%%s\t%%s\\n' ");

    for (ptrdiff_t i = 0; i < arrlen(commands); i++) {
        const char *nm = commands[i].name ? commands[i].name : "";
        const char *ds = commands[i].desc ? commands[i].desc : "";
        char en[256], ed[512];
        shell_escape_single(nm, en, sizeof(en));
        shell_escape_single(ds, ed, sizeof(ed));
        size_t need = strlen(en) + 1 + strlen(ed) + 1;
        if (off + need + 4 >= sizeof(pipebuf))
            break;
        memcpy(pipebuf + off, en, strlen(en));
        off += strlen(en);
        pipebuf[off++] = ' ';
        memcpy(pipebuf + off, ed, strlen(ed));
        off += strlen(ed);
        pipebuf[off++] = ' ';
    }
    pipebuf[off] = '\0';

    /* If args contains a non-empty initial query, seed fzf with it via
     * --query. Used by command-mode double-Tab escalation. */
    char fzf_opts_buf[512];
    char qescaped[256] = "";
    if (args && *args) {
        shell_escape_single(args, qescaped, sizeof(qescaped));
    }
    snprintf(fzf_opts_buf, sizeof(fzf_opts_buf),
             "--delimiter '\t' --with-nth 1 --preview 'echo {2}' "
             "--preview-window right,60%%,wrap%s%s",
             qescaped[0] ? " --query " : "",
             qescaped[0] ? qescaped : "");
    const char *fzf_opts = fzf_opts_buf;
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
    if (tab)
        *tab = '\0';

    /* Pre-fill the active : prompt with "<picked> " and keep it open
     * for the user to type arguments. cmd_cpick is only ever called
     * from within an active colon prompt (Tab→fzf escalation, or via
     * the :c command), so prompt_current() is non-NULL here. */
    Prompt *p = prompt_current();
    if (p) {
        size_t ll = strlen(picked);
        if (ll + 1 >= sizeof(p->buf)) ll = sizeof(p->buf) - 2;
        char tmp[PROMPT_BUF_CAP];
        memcpy(tmp, picked, ll);
        tmp[ll]   = ' ';
        tmp[ll+1] = '\0';
        prompt_set_text(p, tmp, (int)ll + 1);
        ed_set_status_message(":%s", p->buf);
        prompt_keep_open();
    }
    fzf_free(sel, cnt);
}

static void cmd_fzf(const char *args) {
    (void)args;
    qf_clear(&E.qf);
    char **sel = NULL;
    int cnt = 0;
    const char *fzf_opts =
        "--preview '" FZF_FILE_PREVIEW_BODY "' --preview-window right,60%,wrap";
    if (fzf_run_opts(FZF_PROJECT_FILES_CMD, fzf_opts, 0, &sel, &cnt) &&
        cnt > 0 && sel[0] && sel[0][0]) {
        buf_open_or_switch(sel[0], true);
    } else {
        ed_set_status_message("fzf: no selection");
    }
    fzf_free(sel, cnt);
}

static void cmd_recent(const char *args) {
    (void)args;

    int len = recent_files_len(&E.recent_files);
    if (len == 0) {
        ed_set_status_message("No recent files");
        return;
    }

    char cmd_buf[8192];
    int off = 0;

    off += snprintf(cmd_buf + off, sizeof(cmd_buf) - off, "printf '%%s\\n' ");

    for (int i = 0; i < len && off < (int)sizeof(cmd_buf) - 1024; i++) {
        const char *file = recent_files_get(&E.recent_files, i);
        if (!file)
            continue;

        char escaped[1024];
        shell_escape_single(file, escaped, sizeof(escaped));

        size_t need = strlen(escaped) + 2;
        if (off + need >= sizeof(cmd_buf)) {
            break;
        }

        off += snprintf(cmd_buf + off, sizeof(cmd_buf) - off, "%s ", escaped);
    }

    cmd_buf[off] = '\0';

    char **sel = NULL;
    int cnt = 0;
    const char *fzf_opts =
        "--preview '" FZF_FILE_PREVIEW_BODY "' --preview-window right,60%,wrap";

    if (fzf_run_opts(cmd_buf, fzf_opts, 0, &sel, &cnt) && cnt > 0 && sel[0] &&
        sel[0][0]) {
        buf_open_or_switch(sel[0], true);
    } else {
        ed_set_status_message("no selection");
    }

    fzf_free(sel, cnt);
}

/* ------------------------------------------------------------------ */
/* Named pickers registered with src/picker.h                          */
/* ------------------------------------------------------------------ */

/* "buffers": jump to a buffer. Format per row: idx<TAB>name<TAB>mod<TAB>lines. */
static void pick_buffers(const char *seed) {
    (void)seed;
    if (arrlen(E.buffers) <= 0) {
        ed_set_status_message("no buffers");
        return;
    }
    char pipebuf[8192];
    size_t off = 0;
    off += snprintf(pipebuf + off, sizeof(pipebuf) - off,
                    "printf '%%s\t%%s\t%%s\t%%s\\n' ");
    for (int i = 0; i < (int)arrlen(E.buffers); i++) {
        char idxs[16], lines[32];
        snprintf(idxs,  sizeof(idxs),  "%d", i + 1);
        snprintf(lines, sizeof(lines), "%d", E.buffers[i].num_rows);
        const char *nm  = E.buffers[i].title;
        const char *mod = E.buffers[i].dirty ? "*" : "-";
        char eidx[32], enam[512], emod[8], elines[32];
        shell_escape_single(idxs,  eidx,   sizeof(eidx));
        shell_escape_single(nm,    enam,   sizeof(enam));
        shell_escape_single(mod,   emod,   sizeof(emod));
        shell_escape_single(lines, elines, sizeof(elines));
        size_t need = strlen(eidx) + strlen(enam) + strlen(emod)
                    + strlen(elines) + 4;
        if (off + need + 4 >= sizeof(pipebuf)) break;
        off += snprintf(pipebuf + off, sizeof(pipebuf) - off, "%s %s %s %s ",
                        eidx, enam, emod, elines);
    }
    pipebuf[off] = '\0';

    const char *fzf_opts =
        "--delimiter '\\t' --with-nth 2 "
        "--preview 'printf \"buf:%s modified:%s lines:%s\\n\\n\" {1} {3} {4}; "
        "command -v bat >/dev/null 2>&1 && bat --style=plain --color=always "
        "--line-range :200 {2} || sed -n \"1,200p\" {2} 2>/dev/null' "
        "--preview-window right,60%,wrap";
    char **sel = NULL;
    int cnt = 0;
    if (!fzf_run_opts(pipebuf, fzf_opts, 0, &sel, &cnt) || cnt == 0) {
        fzf_free(sel, cnt);
        ed_set_status_message("buffers: canceled");
        return;
    }
    char *picked = sel[0];
    char *tab = strchr(picked, '\t');
    if (tab) *tab = '\0';
    int idx = atoi(picked);
    if (idx >= 1 && idx <= (int)arrlen(E.buffers))
        buf_switch(idx - 1);
    fzf_free(sel, cnt);
}

/* "commands": list every registered :command, prefill the picked name
 * back into the active prompt. cmd_cpick already does exactly this and
 * is used both by the colon prompt's Tab escalation and by the :c
 * command, so we just alias both names to it (see pickers_init). */

/* "keybinds": list every registered keybind. Selection is informational
 * — no canonical action to take on a bind. */
static void pick_keybinds(const char *seed) {
    (void)seed;
    char pipebuf[16384];
    size_t off = 0;
    off += snprintf(pipebuf + off, sizeof(pipebuf) - off,
                    "printf '%%s\t%%s\\n' ");
    int count = keybind_get_count();
    for (int i = 0; i < count; i++) {
        const char *seq = NULL, *desc = NULL, *ft = NULL, *cmdline = NULL;
        int mode = 0;
        if (!keybind_get_at_ext(i, &seq, &desc, &mode, &ft, &cmdline)) continue;
        const char *mp = "";
        if      (mode == MODE_NORMAL)       mp = "[N] ";
        else if (mode == MODE_INSERT)       mp = "[I] ";
        else if (mode == MODE_VISUAL)       mp = "[V] ";
        else if (mode == MODE_VISUAL_BLOCK) mp = "[VB] ";
        else if (mode == MODE_COMMAND)      mp = "[C] ";
        const char *ft_tag = (ft && *ft) ? ft : "*";
        char display[320];
        snprintf(display, sizeof(display), "%s[%s] %s",
                 mp, ft_tag, seq ? seq : "");
        char es[512], ed[512];
        shell_escape_single(display, es, sizeof(es));
        shell_escape_single(desc ? desc : "", ed, sizeof(ed));
        size_t need = strlen(es) + strlen(ed) + 3;
        if (off + need >= sizeof(pipebuf)) break;
        off += snprintf(pipebuf + off, sizeof(pipebuf) - off, "%s %s ", es, ed);
    }
    pipebuf[off] = '\0';
    char **sel = NULL;
    int cnt = 0;
    fzf_run_opts(pipebuf, "--delimiter '\t'", 0, &sel, &cnt);
    fzf_free(sel, cnt);
    ed_set_status_message("keybinds: %d total", count);
}

/* "plugins": list loaded plugins with description in the preview pane. */
static void pick_plugins(const char *seed) {
    (void)seed;
    char pipebuf[16384];
    size_t off = 0;
    off += snprintf(pipebuf + off, sizeof(pipebuf) - off,
                    "printf '%%s\\t%%s\\n' ");
    int count  = plugin_get_count();
    int active = 0;
    for (int i = 0; i < count; i++) {
        const char *name = NULL, *desc = NULL;
        int enabled = 0;
        if (!plugin_get_at(i, &name, &desc, &enabled)) continue;
        if (enabled) active++;
        char display[256];
        snprintf(display, sizeof(display), "[%c] %s",
                 enabled ? 'x' : ' ', name);
        char es[512], ed[512];
        shell_escape_single(display, es, sizeof(es));
        shell_escape_single(desc[0] ? desc : "(no description)", ed, sizeof(ed));
        size_t need = strlen(es) + strlen(ed) + 3;
        if (off + need >= sizeof(pipebuf)) break;
        off += snprintf(pipebuf + off, sizeof(pipebuf) - off, "%s %s ", es, ed);
    }
    pipebuf[off] = '\0';
    const char *fzf_opts =
        "--delimiter '\t' --with-nth 1 "
        "--preview \"printf '%s' {2}\" --preview-window 'right:50%:wrap'";
    char **sel = NULL;
    int cnt = 0;
    fzf_run_opts(pipebuf, fzf_opts, 0, &sel, &cnt);
    fzf_free(sel, cnt);
    ed_set_status_message("plugins: %d loaded, %d enabled", count, active);
}

/* "files": project-files picker, optionally pre-seeded with a query
 * (used by gF on a path under the cursor). */
static void pick_files(const char *seed) {
    char fzf_opts[PATH_MAX * 2 + 256];
    if (seed && *seed) {
        char esc[PATH_MAX * 2];
        shell_escape_single(seed, esc, sizeof(esc));
        snprintf(fzf_opts, sizeof(fzf_opts),
                 "--select-1 --exit-0 --query %s --preview '"
                 FZF_FILE_PREVIEW_BODY "' --preview-window right,60%%,wrap",
                 esc);
    } else {
        snprintf(fzf_opts, sizeof(fzf_opts),
                 "--preview '" FZF_FILE_PREVIEW_BODY
                 "' --preview-window right,60%%,wrap");
    }
    char **sel = NULL;
    int cnt = 0;
    if (fzf_run_opts(FZF_PROJECT_FILES_CMD, fzf_opts, 0, &sel, &cnt) &&
        cnt > 0 && sel[0] && sel[0][0]) {
        buf_open_or_switch(sel[0], true);
    } else {
        ed_set_status_message("files: no selection");
    }
    fzf_free(sel, cnt);
}

static int pickers_init(void) {
    cmd("c",      cmd_cpick,        "pick cmd");
    cmd("fzf",    cmd_fzf,          "pick a file(s)");
    cmd("recent", cmd_recent,       "recent files");
    cmd("hfzf",   cmd_history_fzf,  "fuzzy search command history");
    cmd("jfzf",   cmd_jumplist_fzf, "fuzzy search jump list");

    /* Named pickers core commands invoke through picker_invoke. */
    picker_register("command",  cmd_cpick);     /* colon Tab escalation */
    picker_register("commands", cmd_cpick);     /* :commands lists same set */
    picker_register("keybinds", pick_keybinds);
    picker_register("buffers",  pick_buffers);
    picker_register("plugins",  pick_plugins);
    picker_register("files",    pick_files);
    return 0;
}

const Plugin plugin_pickers = {
    .name   = "pickers",
    .desc   = "fzf pickers (files, recent, palette, history, jump list)",
    .init   = pickers_init,
    .deinit = NULL,
};
