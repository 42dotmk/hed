#ifndef LSP_H
#define LSP_H

#include <sys/select.h>
#include "buffer.h"

/*
 * Minimal Language Server Protocol (LSP) client interface for hed.
 *
 * This is an early sketch meant to define the integration points:
 * - process / fd management (select() integration)
 * - buffer lifecycle notifications
 * - user-facing requests (hover/definition/completion)
 *
 * The implementation in lsp.c currently contains only stubs.
 */

typedef struct LspServer LspServer;

/* Global init/shutdown */
void lsp_init(void);
void lsp_shutdown(void);

/* select() integration: add any LSP fds to the read set and handle events. */
void lsp_fill_fdset(fd_set *set, int *max_fd);
void lsp_handle_readable(const fd_set *set);

/* Buffer lifecycle hooks – to be called from buffer/editor code. */
void lsp_on_buffer_open(Buffer *buf);
void lsp_on_buffer_close(Buffer *buf);
void lsp_on_buffer_save(Buffer *buf);
void lsp_on_buffer_changed(Buffer *buf);

/* User-facing requests (to be wired to commands/keybinds later). */
void lsp_request_hover(Buffer *buf, int line, int col);
void lsp_request_definition(Buffer *buf, int line, int col);
void lsp_request_completion(Buffer *buf, int line, int col);

#endif /* LSP_H */

