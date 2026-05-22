#include "utils/fzf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "commands/cmd_util.h"
#include "utils/term_cmd.h"
#include <limits.h>

int fzf_run_opts(const char *input_cmd, const char *fzf_opts, int multi,
                 char ***out_lines, int *out_count) {
    if (out_lines)
        *out_lines = NULL;
    if (out_count)
        *out_count = 0;
    if (!input_cmd)
        return 0;

    /* Build the "<input_cmd> | fzf [opts]" pipeline. Callers like
     * cmd_cpick (command palette) build input_cmd as a single
     * `printf ...` listing every registered command — easily 4–8 KB.
     * Stack-allocate to fit that comfortably so snprintf doesn't
     * truncate mid-quote and hand the shell an unparseable string. */
    const char *opts = fzf_opts ? fzf_opts : "";
    size_t need = strlen(input_cmd) + strlen(opts) + 32;
    char *pipebuf = malloc(need);
    if (!pipebuf) return 0;
    snprintf(pipebuf, need, "%s | fzf%s %s", input_cmd,
             multi ? " -m" : "", opts);

    int rc = term_cmd_run(pipebuf, out_lines, out_count);
    free(pipebuf);
    return rc;
}

int fzf_run(const char *input_cmd, int multi, char ***out_lines,
            int *out_count) {
    return fzf_run_opts(input_cmd, NULL, multi, out_lines, out_count);
}

int fzf_pick_list(const char **items, int count, int multi, char ***out_lines,
                  int *out_count) {
    if (!items || count <= 0)
        return 0;
    char pipebuf[8192];
    size_t off = 0;
    off += snprintf(pipebuf + off, sizeof(pipebuf) - off, "printf '%%s\\n' ");
    for (int i = 0; i < count; i++) {
        char esc[256];
        shell_escape_single(items[i], esc, sizeof(esc));
        size_t elen = strlen(esc);
        if (off + elen + 2 >= sizeof(pipebuf))
            break;
        memcpy(pipebuf + off, esc, elen);
        off += elen;
        pipebuf[off++] = ' ';
    }
    pipebuf[off] = '\0';
    return fzf_run(pipebuf, multi, out_lines, out_count);
}

void fzf_free(char **lines, int count) {
    /* Delegate to term_cmd utility */
    term_cmd_free(lines, count);
}
