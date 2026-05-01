#ifndef LSP_H
#define LSP_H

#include "buf/buffer.h"
#include <sys/select.h>

typedef struct LspServer LspServer;

/* Global init/shutdown */
void lsp_init(void);
void lsp_shutdown(void);

/* select() integration: add LSP fds and dispatch readable events */
void lsp_fill_fdset(fd_set *set, int *max_fd);
void lsp_handle_readable(const fd_set *set);

/* Buffer lifecycle — called from hooks */
void lsp_on_buffer_open(Buffer *buf);
void lsp_on_buffer_close(Buffer *buf);
void lsp_on_buffer_save(Buffer *buf);
void lsp_on_buffer_changed(Buffer *buf);

/* User-facing requests */
void lsp_request_hover(Buffer *buf, int line, int col);
void lsp_request_definition(Buffer *buf, int line, int col);
void lsp_request_completion(Buffer *buf, int line, int col);

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
int  lsp_cmd_connect(const char *lang, const char *to_addr,
                     const char *from_addr, const char *root_uri);
int  lsp_cmd_disconnect(const char *lang);
void lsp_cmd_status(void);

#endif /* LSP_H */
