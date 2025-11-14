#ifndef FZF_H
#define FZF_H

/* Simple helpers to interact with fzf in a TUI-friendly way */

/* Runs: <input_cmd> | fzf [-m] and returns selected lines (without newlines).
 * - multi = 0 for single, 1 for multi-select.
 * - out_lines receives a heap-allocated array of heap-allocated strings.
 * - out_count receives number of selected lines.
 * Returns 1 on success (even if 0 selections), 0 on failure to spawn. */
int fzf_run(const char *input_cmd, int multi, char ***out_lines, int *out_count);
/* Same as fzf_run, but allows passing extra fzf options (e.g., preview) */
int fzf_run_opts(const char *input_cmd, const char *fzf_opts, int multi, char ***out_lines, int *out_count);

/* Presents a fixed list of items to fzf.
 * Builds a printable pipeline and delegates to fzf_run().
 */
int fzf_pick_list(const char **items, int count, int multi, char ***out_lines, int *out_count);

/* Free the results allocated by fzf_run / fzf_pick_list. */
void fzf_free(char **lines, int count);

#endif /* FZF_H */
