#ifndef LSP_H
#define LSP_H

#include "buf/buffer.h"

typedef struct LspServer LspServer;

/* Global init/shutdown */
void lsp_init(void);
void lsp_shutdown(void);

/* fd readiness is handled inside the plugin via ed_loop_register; no
 * select-loop entry points are exported. */

/* Buffer lifecycle — called from hooks */
void lsp_on_buffer_open(Buffer *buf);
void lsp_on_buffer_close(Buffer *buf);
void lsp_on_buffer_save(Buffer *buf);
void lsp_on_buffer_changed(Buffer *buf);
/* Send a full-text didChange only if the buffer changed since the last
 * sync. Called before every request; cheap when clean. */
void lsp_sync_document(Buffer *buf);

/* User-facing requests. Columns are byte offsets; the UTF-16
 * conversion the protocol wants happens inside. */
void lsp_request_hover(Buffer *buf, int line, int col);
void lsp_request_definition(Buffer *buf, int line, int col);

/* Probe for the :definition dispatcher (plugins/ctags declares this
 * weak): request an LSP definition for the current buffer if a ready
 * server is attached. 0 = request sent, -1 = fall back to ctags. */
int lsp_definition_try(void);

/* Register the LSP completion source with plugins/completion. */
void lsp_completion_source_register(void);

/* Connect to a manually-started LSP server.
 *
 * Named pipes  – two separate paths:
 *   lsp_cmd_connect("c", "/tmp/lsp-in", "/tmp/lsp-out", NULL)
 *   :lsp_connect c /tmp/lsp-in /tmp/lsp-out
 *
 * TCP socket   – pass NULL for from_addr, embed port in to_addr:
 *   lsp_cmd_connect("c", "tcp", "127.0.0.1:9090", NULL)
 *   :lsp_connect c tcp 127.0.0.1:9090
 */
int lsp_cmd_connect(const char *lang, const char *to_addr,
                    const char *from_addr, const char *root_uri);

/* Spawn a server from the registry in plugins/lsp/lsp_servers.c.
 *   :lsp_start c
 * `hint_path` (NULL or "" → E.cwd) seeds root detection. */
int lsp_cmd_start(const char *lang, const char *hint_path);

int lsp_cmd_disconnect(const char *lang);
void lsp_cmd_status(void);

/* Dump all stored diagnostics into E.qf and open the quickfix pane. */
void lsp_cmd_diagnostics(void);

#endif /* LSP_H */
