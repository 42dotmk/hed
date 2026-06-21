/* mcp_server plugin: exposes hed as a Model Context Protocol server.
 *
 * Transport: Unix domain stream socket, newline-delimited JSON-RPC 2.0.
 *   (MCP's stdio transport is byte-identical apart from the socket vs
 *   stdin/stdout choice — a tiny `socat` bridge connects any stdio MCP
 *   host to this socket.)
 *
 *   $ socat - UNIX-CONNECT:/tmp/hed-mcp-<pid>.sock
 *
 * Commands:
 *   :mcp_start    bind the listening socket + announce path
 *   :mcp_stop     close listener + drop clients
 *   :mcp_status   print socket path + connected-client count
 *
 * Tools exposed (first slice):
 *   read_current_buffer()
 *   apply_edit(start_line:int, end_line:int, text:string)
 *   run_command(cmdline:string)
 *
 * Method support: initialize, notifications/initialized, tools/list,
 * tools/call. Everything else returns method-not-found. */

#include "hed.h"
#include "mcp_server.h"
#include "select_loop.h"
#include "lsp/cjson/cJSON.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

/* Global helper from buf/buffer.c. */

#define MCP_MAX_CLIENTS 8
#define MCP_RX_LIMIT    (4 * 1024 * 1024)  /* per-client cap on accumulator */

typedef struct McpClient {
    int    fd;          /* 0 = slot free, otherwise an open fd */
    char  *rx;
    size_t rx_len, rx_cap;
} McpClient;

static int      g_listen_fd = -1;
static char     g_socket_path[108] = {0};
static McpClient g_clients[MCP_MAX_CLIENTS];

/* ---- transport ---- */

static void client_close(McpClient *c) {
    if (!c || c->fd <= 0) return;
    ed_loop_unregister(c->fd);
    close(c->fd);
    free(c->rx);
    memset(c, 0, sizeof(*c));
}

static McpClient *client_for_fd(int fd) {
    for (int i = 0; i < MCP_MAX_CLIENTS; i++)
        if (g_clients[i].fd == fd) return &g_clients[i];
    return NULL;
}

static void send_message(int fd, cJSON *msg) {
    char *s = cJSON_PrintUnformatted(msg);
    if (!s) return;
    size_t n = strlen(s);
    /* Best-effort write; if the client is gone we'll notice on next read. */
    (void)!write(fd, s, n);
    (void)!write(fd, "\n", 1);
    free(s);
}

static cJSON *make_response(cJSON *id, cJSON *result) {
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "jsonrpc", "2.0");
    if (id) cJSON_AddItemReferenceToObject(r, "id", id);
    else    cJSON_AddNullToObject(r, "id");
    cJSON_AddItemToObject(r, "result", result ? result : cJSON_CreateObject());
    return r;
}

static cJSON *make_error(cJSON *id, int code, const char *message) {
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "jsonrpc", "2.0");
    if (id) cJSON_AddItemReferenceToObject(r, "id", id);
    else    cJSON_AddNullToObject(r, "id");
    cJSON *err = cJSON_CreateObject();
    cJSON_AddNumberToObject(err, "code", code);
    cJSON_AddStringToObject(err, "message", message ? message : "error");
    cJSON_AddItemToObject(r, "error", err);
    return r;
}

/* ---- tool descriptors ---- */

static cJSON *empty_object_schema(void) {
    cJSON *s = cJSON_CreateObject();
    cJSON_AddStringToObject(s, "type", "object");
    cJSON_AddItemToObject(s, "properties", cJSON_CreateObject());
    return s;
}

static cJSON *tool_descriptors(void) {
    cJSON *arr = cJSON_CreateArray();

    /* read_current_buffer */
    {
        cJSON *t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "name", "read_current_buffer");
        cJSON_AddStringToObject(t, "description",
            "Return the full text of the currently focused buffer.");
        cJSON_AddItemToObject(t, "inputSchema", empty_object_schema());
        cJSON_AddItemToArray(arr, t);
    }

    /* apply_edit */
    {
        cJSON *t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "name", "apply_edit");
        cJSON_AddStringToObject(t, "description",
            "Replace lines [start_line, end_line) of the current buffer "
            "with `text` (LF-separated). 0-indexed, half-open. "
            "Goes through hed's normal edit + undo path; hooks fire.");
        cJSON *s = cJSON_CreateObject();
        cJSON_AddStringToObject(s, "type", "object");
        cJSON *props = cJSON_CreateObject();
        cJSON *p;
        p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "type", "integer");
        cJSON_AddItemToObject(props, "start_line", p);
        p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "type", "integer");
        cJSON_AddItemToObject(props, "end_line", p);
        p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "type", "string");
        cJSON_AddItemToObject(props, "text", p);
        cJSON_AddItemToObject(s, "properties", props);
        cJSON *req = cJSON_CreateArray();
        cJSON_AddItemToArray(req, cJSON_CreateString("start_line"));
        cJSON_AddItemToArray(req, cJSON_CreateString("end_line"));
        cJSON_AddItemToArray(req, cJSON_CreateString("text"));
        cJSON_AddItemToObject(s, "required", req);
        cJSON_AddItemToObject(t, "inputSchema", s);
        cJSON_AddItemToArray(arr, t);
    }

    /* run_command */
    {
        cJSON *t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "name", "run_command");
        cJSON_AddStringToObject(t, "description",
            "Invoke a hed `:` command. `cmdline` is what you'd type after "
            "the colon (e.g. \"w\", \"e src/foo.c\", \"rg TODO\").");
        cJSON *s = cJSON_CreateObject();
        cJSON_AddStringToObject(s, "type", "object");
        cJSON *props = cJSON_CreateObject();
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "type", "string");
        cJSON_AddItemToObject(props, "cmdline", p);
        cJSON_AddItemToObject(s, "properties", props);
        cJSON *req = cJSON_CreateArray();
        cJSON_AddItemToArray(req, cJSON_CreateString("cmdline"));
        cJSON_AddItemToObject(s, "required", req);
        cJSON_AddItemToObject(t, "inputSchema", s);
        cJSON_AddItemToArray(arr, t);
    }

    return arr;
}

/* ---- tool implementations ---- */

static cJSON *tool_result_text(const char *text, int is_error) {
    cJSON *result = cJSON_CreateObject();
    cJSON *content = cJSON_CreateArray();
    cJSON *block = cJSON_CreateObject();
    cJSON_AddStringToObject(block, "type", "text");
    cJSON_AddStringToObject(block, "text", text ? text : "");
    cJSON_AddItemToArray(content, block);
    cJSON_AddItemToObject(result, "content", content);
    if (is_error) cJSON_AddBoolToObject(result, "isError", 1);
    return result;
}

static cJSON *tool_read_current_buffer(void) {
    Buffer *b = buf_cur();
    if (!b) return tool_result_text("error: no current buffer", 1);

    char *out = buf_to_text(b, NULL);
    if (!out) return tool_result_text("error: out of memory", 1);

    cJSON *r = tool_result_text(out, 0);
    free(out);
    return r;
}

static cJSON *tool_apply_edit(cJSON *args) {
    Buffer *b = buf_cur();
    if (!b)
        return tool_result_text("error: no current buffer", 1);
    if (b->readonly)
        return tool_result_text("error: buffer is read-only", 1);

    cJSON *js = cJSON_GetObjectItem(args, "start_line");
    cJSON *je = cJSON_GetObjectItem(args, "end_line");
    cJSON *jt = cJSON_GetObjectItem(args, "text");
    if (!cJSON_IsNumber(js) || !cJSON_IsNumber(je) || !cJSON_IsString(jt))
        return tool_result_text(
            "error: expected start_line:int, end_line:int, text:string", 1);

    int start = js->valueint;
    int end   = je->valueint;
    if (start < 0) start = 0;
    if (end < start) end = start;
    if (end > b->num_rows) end = b->num_rows;

    /* Delete [start, end) — buf_row_del_in takes one index at a time and
     * shifts the tail, so repeatedly delete at `start`. */
    for (int i = start; i < end; i++)
        buf_row_del_in(b, start);

    /* Insert lines from `text` at `start`. */
    const char *text = jt->valuestring;
    size_t len = text ? strlen(text) : 0;
    int    at  = start;
    int    inserted = 0;
    size_t lstart = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || text[i] == '\n') {
            size_t llen = i - lstart;
            if (llen && text[lstart + llen - 1] == '\r') llen--;
            buf_row_insert_in(b, at++, text + lstart, llen);
            inserted++;
            lstart = i + 1;
            /* If `text` is empty, don't insert anything. */
            if (i == len && len == 0) inserted = 0;
        }
    }
    /* A trailing newline in `text` would have inserted a final empty line;
     * that matches the convention that `text` is the LF-terminated content
     * of the range. If you didn't want that, omit the trailing \n. */

    char msg[128];
    snprintf(msg, sizeof(msg),
             "ok: replaced %d lines [%d,%d) with %d lines",
             end - start, start, end, inserted);
    ed_set_status_message("mcp: apply_edit [%d,%d) -> %d lines",
                          start, end, inserted);
    return tool_result_text(msg, 0);
}

static cJSON *tool_run_command(cJSON *args) {
    cJSON *jc = cJSON_GetObjectItem(args, "cmdline");
    if (!cJSON_IsString(jc))
        return tool_result_text("error: cmdline:string required", 1);

    const char *line = jc->valuestring;
    while (*line == ' ' || *line == '\t' || *line == ':') line++;
    if (!*line) return tool_result_text("error: empty cmdline", 1);

    char buf[256];
    if (!command_execute_line(line)) {
        snprintf(buf, sizeof(buf), "error: unknown command :%s", line);
        return tool_result_text(buf, 1);
    }
    snprintf(buf, sizeof(buf), "ok: ran :%s", line);
    return tool_result_text(buf, 0);
}

/* ---- JSON-RPC dispatch ---- */

static cJSON *handle_initialize(cJSON *id) {
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "protocolVersion", "2024-11-05");
    cJSON *caps = cJSON_CreateObject();
    cJSON *tools_cap = cJSON_CreateObject();
    cJSON_AddBoolToObject(tools_cap, "listChanged", 0);
    cJSON_AddItemToObject(caps, "tools", tools_cap);
    cJSON_AddItemToObject(result, "capabilities", caps);
    cJSON *info = cJSON_CreateObject();
    cJSON_AddStringToObject(info, "name", "hed");
    cJSON_AddStringToObject(info, "version", "0.1");
    cJSON_AddItemToObject(result, "serverInfo", info);
    return make_response(id, result);
}

static cJSON *handle_tools_list(cJSON *id) {
    cJSON *result = cJSON_CreateObject();
    cJSON_AddItemToObject(result, "tools", tool_descriptors());
    return make_response(id, result);
}

static cJSON *handle_tools_call(cJSON *id, cJSON *params) {
    cJSON *jname = cJSON_GetObjectItem(params, "name");
    cJSON *jargs = cJSON_GetObjectItem(params, "arguments");
    if (!cJSON_IsString(jname))
        return make_error(id, -32602, "tools/call: missing name");
    const char *name = jname->valuestring;
    cJSON *result;
    if (strcmp(name, "read_current_buffer") == 0)
        result = tool_read_current_buffer();
    else if (strcmp(name, "apply_edit") == 0)
        result = tool_apply_edit(jargs);
    else if (strcmp(name, "run_command") == 0)
        result = tool_run_command(jargs);
    else {
        char err[128];
        snprintf(err, sizeof(err), "unknown tool: %s", name);
        return make_error(id, -32601, err);
    }
    return make_response(id, result);
}

static void dispatch_message(int fd, const char *json, size_t len) {
    cJSON *msg = cJSON_ParseWithLength(json, len);
    if (!msg) {
        cJSON *err = make_error(NULL, -32700, "parse error");
        send_message(fd, err);
        cJSON_Delete(err);
        return;
    }

    cJSON *jmethod = cJSON_GetObjectItem(msg, "method");
    cJSON *jid     = cJSON_GetObjectItem(msg, "id");
    cJSON *jparams = cJSON_GetObjectItem(msg, "params");
    int    is_notification = (jid == NULL);

    if (!cJSON_IsString(jmethod)) {
        if (!is_notification) {
            cJSON *err = make_error(jid, -32600, "method must be string");
            send_message(fd, err);
            cJSON_Delete(err);
        }
        cJSON_Delete(msg);
        return;
    }
    const char *method = jmethod->valuestring;

    cJSON *resp = NULL;
    if (strcmp(method, "initialize") == 0) {
        resp = handle_initialize(jid);
    } else if (strcmp(method, "notifications/initialized") == 0 ||
               strncmp(method, "notifications/", 14) == 0) {
        /* Notifications get no response. */
    } else if (strcmp(method, "tools/list") == 0) {
        resp = handle_tools_list(jid);
    } else if (strcmp(method, "tools/call") == 0) {
        resp = handle_tools_call(jid, jparams);
    } else if (strcmp(method, "ping") == 0) {
        resp = make_response(jid, cJSON_CreateObject());
    } else if (!is_notification) {
        char err[128];
        snprintf(err, sizeof(err), "method not found: %s", method);
        resp = make_error(jid, -32601, err);
    }

    if (resp) {
        send_message(fd, resp);
        cJSON_Delete(resp);
    }
    cJSON_Delete(msg);
}

/* ---- fd callbacks ---- */

static void drain_client(int fd, void *ud) {
    (void)ud;
    McpClient *c = client_for_fd(fd);
    if (!c) return;

    char chunk[4096];
    ssize_t n = read(fd, chunk, sizeof(chunk));
    if (n <= 0) {
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) return;
        client_close(c);
        return;
    }

    if (c->rx_len + (size_t)n + 1 > c->rx_cap) {
        size_t ncap = c->rx_cap ? c->rx_cap * 2 : 4096;
        while (ncap < c->rx_len + (size_t)n + 1) ncap *= 2;
        if (ncap > MCP_RX_LIMIT) { client_close(c); return; }
        char *r = realloc(c->rx, ncap);
        if (!r) { client_close(c); return; }
        c->rx = r;
        c->rx_cap = ncap;
    }
    memcpy(c->rx + c->rx_len, chunk, (size_t)n);
    c->rx_len += (size_t)n;
    c->rx[c->rx_len] = '\0';

    /* Split on '\n'; dispatch each complete line. */
    size_t off = 0;
    for (size_t i = 0; i < c->rx_len; i++) {
        if (c->rx[i] == '\n') {
            size_t lend = i;
            /* Strip trailing \r. */
            if (lend > off && c->rx[lend - 1] == '\r') lend--;
            if (lend > off)
                dispatch_message(fd, c->rx + off, lend - off);
            off = i + 1;
        }
    }
    if (off > 0) {
        memmove(c->rx, c->rx + off, c->rx_len - off);
        c->rx_len -= off;
    }
}

static void on_accept(int listen_fd, void *ud) {
    (void)ud;
    int fd = accept(listen_fd, NULL, NULL);
    if (fd < 0) return;

    /* Find a free slot. */
    McpClient *slot = NULL;
    for (int i = 0; i < MCP_MAX_CLIENTS; i++)
        if (g_clients[i].fd == 0) { slot = &g_clients[i]; break; }
    if (!slot) {
        close(fd);
        ed_set_status_message("mcp: client limit reached");
        return;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    slot->fd = fd;
    slot->rx = NULL;
    slot->rx_len = slot->rx_cap = 0;
    ed_loop_register("mcp.client", fd, drain_client, NULL);
}

/* ---- listener lifecycle ---- */

static int mcp_start(void) {
    if (g_listen_fd >= 0) return 0;

    snprintf(g_socket_path, sizeof(g_socket_path),
             "/tmp/hed-mcp-%d.sock", (int)getpid());
    unlink(g_socket_path);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", g_socket_path);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(fd, 4) != 0) {
        close(fd);
        return -1;
    }
    chmod(g_socket_path, 0600);

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    g_listen_fd = fd;
    ed_loop_register("mcp.listen", fd, on_accept, NULL);
    return 0;
}

static void mcp_stop(void) {
    for (int i = 0; i < MCP_MAX_CLIENTS; i++)
        client_close(&g_clients[i]);
    if (g_listen_fd >= 0) {
        ed_loop_unregister(g_listen_fd);
        close(g_listen_fd);
        g_listen_fd = -1;
    }
    if (g_socket_path[0]) {
        unlink(g_socket_path);
        g_socket_path[0] = '\0';
    }
}

/* ---- :commands ---- */

static void cmd_mcp_start(const char *args) {
    (void)args;
    if (g_listen_fd >= 0) {
        ed_set_status_message("mcp: already running at %s", g_socket_path);
        return;
    }
    if (mcp_start() != 0) {
        ed_set_status_message("mcp: failed to start (%s)", strerror(errno));
        return;
    }
    ed_set_status_message("mcp: listening on %s", g_socket_path);
}

static void cmd_mcp_stop(const char *args) {
    (void)args;
    if (g_listen_fd < 0) {
        ed_set_status_message("mcp: not running");
        return;
    }
    mcp_stop();
    ed_set_status_message("mcp: stopped");
}

static void cmd_mcp_status(const char *args) {
    (void)args;
    if (g_listen_fd < 0) {
        ed_set_status_message("mcp: not running");
        return;
    }
    int n = 0;
    for (int i = 0; i < MCP_MAX_CLIENTS; i++)
        if (g_clients[i].fd > 0) n++;
    ed_set_status_message("mcp: %s (%d client%s)",
                          g_socket_path, n, n == 1 ? "" : "s");
}

/* ---- plugin lifecycle ---- */

static int mcp_init(void) {
    cmd("mcp_start",  cmd_mcp_start,  "start the MCP server");
    cmd("mcp_stop",   cmd_mcp_stop,   "stop the MCP server");
    cmd("mcp_status", cmd_mcp_status, "show MCP server status");
    return 0;
}

static void mcp_deinit(void) {
    mcp_stop();
}

const Plugin plugin_mcp_server = {
    .name   = "mcp_server",
    .desc   = "expose hed as a Model Context Protocol server",
    .init   = mcp_init,
    .deinit = mcp_deinit,
};
