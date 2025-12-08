#include "hed.h"
#include "cmd_util.h"
#include "fzf.h"
#include "term_cmd.h"
#include <limits.h>

int fzf_run_opts(const char *input_cmd, const char *fzf_opts, int multi, char ***out_lines, int *out_count) {
    if (out_lines) *out_lines = NULL;
    if (out_count) *out_count = 0;
    if (!input_cmd) return 0;

    /* Build fzf command pipeline */
    char pipebuf[4096];
    snprintf(pipebuf, sizeof(pipebuf), "%s | fzf%s %s",
             input_cmd, multi ? " -m" : "", fzf_opts ? fzf_opts : "");

    /* Use term_cmd utility to run fzf */
    return term_cmd_run(pipebuf, out_lines, out_count);
}

int fzf_run(const char *input_cmd, int multi, char ***out_lines, int *out_count) {
    return fzf_run_opts(input_cmd, NULL, multi, out_lines, out_count);
}

int fzf_pick_list(const char **items, int count, int multi, char ***out_lines, int *out_count) {
    if (!items || count <= 0) return 0;
    char pipebuf[8192]; size_t off = 0;
    off += snprintf(pipebuf + off, sizeof(pipebuf) - off, "printf '%%s\\n' ");
    for (int i = 0; i < count; i++) {
        char esc[256]; shell_escape_single(items[i], esc, sizeof(esc));
        size_t elen = strlen(esc);
        if (off + elen + 2 >= sizeof(pipebuf)) break;
        memcpy(pipebuf + off, esc, elen); off += elen;
        pipebuf[off++] = ' ';
    }
    pipebuf[off] = '\0';
    return fzf_run(pipebuf, multi, out_lines, out_count);
}

void fzf_free(char **lines, int count) {
    /* Delegate to term_cmd utility */
    term_cmd_free(lines, count);
}
