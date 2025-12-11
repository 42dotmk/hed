#include "lsp.h"
#include "hed.h"

#include <sys/select.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * Minimal LSP client skeleton.
 *
 * This file intentionally contains only scaffolding and stubs. The goal is
 * to define a clean place in the codebase where LSP support will live,
 * without yet committing to a concrete JSON/LSP implementation.
 *
 * Planned responsibilities:
 *  - Track one LspServer per language/filetype.
 *  - Start/stop LSP server processes and manage their pipes.
 *  - Serialize/deserialize JSON-RPC messages (Content-Length framing).
 *  - Bridge editor events (buffer open/change/save) to LSP notifications.
 *  - Handle diagnostics and navigation/completion requests.
 */

struct LspServer {
    char *lang;      /* filetype / language id, e.g. "c", "python" */
    char *root_uri;  /* workspace root (file:// URI) */
    pid_t pid;       /* LSP server process id */
    int to_fd;       /* write end (editor -> server stdin) */
    int from_fd;     /* read end (server stdout -> editor) */
    int initialized; /* 1 after successful initialize/initialized */
    int next_id;     /* next JSON-RPC id for requests */
};

static LspServer *g_servers = NULL;
static int g_servers_len = 0;

/* Placeholder: later this will find or spawn a server for a given buffer. */
static LspServer *lsp_server_for_buffer(Buffer *buf) {
    (void)buf;
    return NULL;
}

void lsp_init(void) {
    g_servers = NULL;
    g_servers_len = 0;
}

void lsp_shutdown(void) {
    /* TODO: terminate any running servers and free resources. */
    (void)g_servers;
    (void)g_servers_len;
}

void lsp_fill_fdset(fd_set *set, int *max_fd) {
    (void)set;
    (void)max_fd;
    /* TODO: once servers are running, add from_fd for each into the fd_set. */
}

void lsp_handle_readable(const fd_set *set) {
    (void)set;
    /* TODO: read from any ready from_fd, parse LSP messages, and dispatch. */
}

void lsp_on_buffer_open(Buffer *buf) {
    (void)buf;
    /* TODO: ensure server for this buffer and send textDocument/didOpen. */
}

void lsp_on_buffer_close(Buffer *buf) {
    (void)buf;
    /* TODO: send textDocument/didClose; optionally tear down unused servers. */
}

void lsp_on_buffer_save(Buffer *buf) {
    (void)buf;
    /* TODO: send textDocument/didSave notification. */
}

void lsp_on_buffer_changed(Buffer *buf) {
    (void)buf;
    /* TODO: queue and eventually send textDocument/didChange. */
}

void lsp_request_hover(Buffer *buf, int line, int col) {
    (void)buf;
    (void)line;
    (void)col;
    /* TODO: send textDocument/hover and display the result. */
    ed_set_status_message("LSP hover (stub)");
}

void lsp_request_definition(Buffer *buf, int line, int col) {
    (void)buf;
    (void)line;
    (void)col;
    /* TODO: send textDocument/definition and jump to the result. */
    ed_set_status_message("LSP goto definition (stub)");
}

void lsp_request_completion(Buffer *buf, int line, int col) {
    (void)buf;
    (void)line;
    (void)col;
    /* TODO: send textDocument/completion and show completion menu. */
    ed_set_status_message("LSP completion (stub)");
}
