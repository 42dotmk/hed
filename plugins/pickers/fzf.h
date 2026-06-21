#ifndef PICKERS_FZF_H
#define PICKERS_FZF_H

#include "lib/strbuf.h"

/* Low-level fzf wrappers. Lives inside the pickers plugin because
 * pickers owns the fzf integration end-to-end.
 *
 * Most plugins should NOT include this header — they should call
 * picker_list() from src/input/picker.h, which routes through
 * whichever picker backend is registered (fzf by default). Reach for
 * this header only when you need fzf-specific options (e.g. live
 * --bind change:reload, --preview) that the generic picker_list
 * abstraction can't express. */

/* Runs: <input_cmd> | fzf [-m] and returns selected lines (without newlines).
 * - multi = 0 for single, 1 for multi-select.
 * - out_lines receives a heap-allocated array of heap-allocated strings.
 * - out_count receives number of selected lines.
 * Returns 1 on success (even if 0 selections), 0 on failure to spawn. */
int fzf_run(const char *input_cmd, int multi, char ***out_lines,
            int *out_count);
/* Same as fzf_run, but allows passing extra fzf options (e.g., preview) */
int fzf_run_opts(const char *input_cmd, const char *fzf_opts, int multi,
                 char ***out_lines, int *out_count);

/* Presents a fixed list of items to fzf.
 * Builds a printable pipeline and delegates to fzf_run().
 */
int fzf_pick_list(const char **items, int count, int multi, char ***out_lines,
                  int *out_count);

/* Free the results allocated by fzf_run / fzf_pick_list. */
void fzf_free(char **lines, int count);

/* Tab-separated-column fzf input builder.
 *
 * Builds a `printf '%s\t…%s\n' f f f …` pipeline where every row
 * contributes `ncols` shell-escaped, tab-separated columns. Grows
 * unbounded — replaces the fixed pipebuf + per-field overflow guards
 * the pickers used to hand-roll. Typical use:
 *
 *     FzfInput in;
 *     fzf_input_init(&in, 2);
 *     const char *row[2] = { name, desc };
 *     fzf_input_row(&in, row);
 *     fzf_run_opts(fzf_input_cmd(&in), opts, 0, &sel, &cnt);
 *     fzf_input_free(&in);
 */
typedef struct {
    StrBuf cmd;
    int      ncols;
} FzfInput;

void        fzf_input_init(FzfInput *in, int ncols);
void        fzf_input_row(FzfInput *in, const char *const *fields);
const char *fzf_input_cmd(FzfInput *in); /* valid until fzf_input_free */
void        fzf_input_free(FzfInput *in);

/* Shared shell snippets used by the file-picker callers (`:fzf`, `:recent`,
 * the gF keybind). Single source of truth for "how do we list project files"
 * and "what does the file preview look like." */

/* Lists every file under the cwd, preferring ripgrep when present. */
#define FZF_PROJECT_FILES_CMD                                                 \
    "(command -v rg >/dev/null 2>&1 && rg --files || find . -type f -print)"

/* Inner shell body for the fzf --preview option. fzf substitutes {} with
 * the line under the cursor. Bat if installed, sed fallback. Caller wraps
 * this in --preview '...' and adds --preview-window. */
#define FZF_FILE_PREVIEW_BODY                                                 \
    "command -v bat >/dev/null 2>&1 && bat --style=plain "                    \
    "--color=always --line-range :200 {} || sed -n \"1,200p\" {} 2>/dev/null"

#endif /* PICKERS_FZF_H */
