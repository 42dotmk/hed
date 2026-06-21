#include "pickers/fzf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

void fzf_input_init(FzfInput *in, int ncols) {
    in->ncols = ncols < 1 ? 1 : ncols;
    in->cmd   = strbuf_new();
    strbuf_append(&in->cmd, "printf '", 8);
    for (int i = 0; i < in->ncols; i++) {
        if (i)
            strbuf_append_char(&in->cmd, '\t');
        strbuf_append(&in->cmd, "%s", 2);
    }
    strbuf_append(&in->cmd, "\\n' ", 4);
}

void fzf_input_row(FzfInput *in, const char *const *fields) {
    for (int i = 0; i < in->ncols; i++) {
        strbuf_append_shell_quoted(&in->cmd, fields[i] ? fields[i] : "");
        strbuf_append_char(&in->cmd, ' ');
    }
}

const char *fzf_input_cmd(FzfInput *in) {
    /* NUL-terminate without counting the terminator in len, so any
     * further rows overwrite it. */
    strbuf_append_char(&in->cmd, '\0');
    in->cmd.len--;
    return in->cmd.data;
}

void fzf_input_free(FzfInput *in) { strbuf_free(&in->cmd); }

int fzf_pick_list(const char **items, int count, int multi, char ***out_lines,
                  int *out_count) {
    if (!items || count <= 0)
        return 0;
    FzfInput in;
    fzf_input_init(&in, 1);
    for (int i = 0; i < count; i++) {
        const char *row[1] = { items[i] };
        fzf_input_row(&in, row);
    }
    int rc = fzf_run(fzf_input_cmd(&in), multi, out_lines, out_count);
    fzf_input_free(&in);
    return rc;
}

void fzf_free(char **lines, int count) {
    /* Delegate to term_cmd utility */
    term_cmd_free(lines, count);
}
