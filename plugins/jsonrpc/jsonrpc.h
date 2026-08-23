#ifndef JSONRPC_H
#define JSONRPC_H

#include "lsp/cjson/cJSON.h"
#include <stddef.h>

/* Shared JSON-RPC 2.0 plumbing for the plugins that speak it (lsp,
 * copilot, mcp_server). Not a plugin — just code compiled into the
 * binary, like the vendored cJSON it builds on. Covers the two halves
 * every user re-implemented: Content-Length framing on the read side
 * and envelope building + reliable writes on the send side. */

/* --- Content-Length framing reader (LSP-style) --------------------- */

typedef struct JrpcReader {
    char *buf; /* growable accumulator, always NUL-terminated */
    size_t len, cap;
    long content_length; /* -1 = waiting for the header block */
} JrpcReader;

/* Zero-init a JrpcReader (or memset) and it's ready; content_length
 * must start at -1 — jrpc_reader_init does both. */
void jrpc_reader_init(JrpcReader *r);
void jrpc_reader_free(JrpcReader *r);

/* Append n raw bytes from the wire. Returns 0 on OOM (bytes dropped). */
int jrpc_reader_feed(JrpcReader *r, const char *data, size_t n);

/* Pop the next complete message body: malloc'd, NUL-terminated,
 * length in *out_len (optional). NULL while no full message is
 * buffered. Malformed header blocks drop the buffered bytes. */
char *jrpc_reader_next(JrpcReader *r, size_t *out_len);

/* --- envelopes ------------------------------------------------------ */

/* Build {"jsonrpc":"2.0", ...}. `params`/`result` are adopted (owned
 * by the returned object); `req_id` is duplicated. Caller sends and/or
 * deletes the result. */
cJSON *jrpc_request(const char *method, cJSON *params, int id);
cJSON *jrpc_notification(const char *method, cJSON *params);
cJSON *jrpc_response(const cJSON *req_id, cJSON *result);
cJSON *jrpc_error(const cJSON *req_id, int code, const char *message);

/* Serialize `msg`, frame it with "Content-Length: N\r\n\r\n", and
 * write the whole thing to fd — retrying on EINTR and polling for
 * writability on EAGAIN (nonblocking fds). Deletes `msg`. Returns 0,
 * or -1 on write/serialize failure. */
int jrpc_send(int fd, cJSON *msg);

#endif /* JSONRPC_H */
