#ifndef LSP_SERVERS_H
#define LSP_SERVERS_H

/* Registry of known LSP servers, keyed by hed filetype.
 *
 * Each entry describes how to *spawn* a server: the argv to exec, and a
 * NULL-terminated list of root markers used to detect the project root
 * by walking up from a buffer's path. An entry is considered installed
 * if its argv[0] is on $PATH.
 *
 * The list is intentionally hardcoded in C — same stance as
 * plugins/fmt/fmt.c. Add an entry here, recompile, done. */

typedef struct {
    const char        *lang;          /* matches Buffer.filetype */
    const char *const *argv;          /* NULL-terminated execvp argv */
    const char *const *root_markers;  /* NULL-terminated, e.g. {".git", "Cargo.toml", NULL} */
} LspServerDef;

/* Look up a server definition by filetype. NULL if no entry. */
const LspServerDef *lsp_servers_lookup(const char *lang);

#endif /* LSP_SERVERS_H */
