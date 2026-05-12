/* pickers plugin: fzf-driven views over editor state — command history
 * and the jump list. Pure UI on top of utils/history.c and
 * utils/jump_list.c; no core editing primitives live here. */

#include "hed.h"
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

    char tmppath[64];
    snprintf(tmppath, sizeof(tmppath), "/tmp/hed_hist_%d", getpid());
    FILE *fp = fopen(tmppath, "w");
    if (!fp) {
        ed_set_status_message("hfzf: failed to write temp file");
        return;
    }
    for (int i = 0; i < hlen; i++) {
        const char *line = hist_get(&E.history, i);
        if (line) fprintf(fp, "%s\n", line);
    }
    fclose(fp);

    char fzf_cmd[128];
    snprintf(fzf_cmd, sizeof(fzf_cmd), "cat %s", tmppath);

    char **sel = NULL;
    int cnt    = 0;
    fzf_run(fzf_cmd, 0, &sel, &cnt);
    unlink(tmppath);

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

    char tmppath[64];
    snprintf(tmppath, sizeof(tmppath), "/tmp/hed_jumps_%d", getpid());
    FILE *fp = fopen(tmppath, "w");
    if (!fp) {
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

    char fzf_cmd[128];
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
    unlink(tmppath);

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

static int pickers_init(void) {
    cmd("hfzf", cmd_history_fzf, "fuzzy search command history");
    cmd("jfzf", cmd_jumplist_fzf, "fuzzy search jump list");
    return 0;
}

const Plugin plugin_pickers = {
    .name   = "pickers",
    .desc   = "fzf pickers over editor state (history, jump list)",
    .init   = pickers_init,
    .deinit = NULL,
};
