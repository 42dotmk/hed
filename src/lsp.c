#include "lsp.h"
#include "editor.h"
#include "json_helpers.h"
#include "log.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * LSP client implementation for hed.
 *
 * This implements the Language Server Protocol client that communicates
 * with LSP servers over stdio using JSON-RPC 2.0 messages.
 *
 * Protocol overview:
 *  - Messages are framed with "Content-Length: <bytes>\r\n\r\n<json>"
 *  - JSON-RPC 2.0 for request/response/notification
 *  - LSP lifecycle: initialize -> initialized -> textDocument notifications
 */

#define LSP_MAX_SERVERS 8
#define LSP_READ_BUF_SIZE 8192
#define LSP_WRITE_BUF_SIZE 8192

/* LSP server state */
struct LspServer {
    char *lang;       /* filetype / language id, e.g. "c", "python" */
    char *root_uri;   /* workspace root (file:// URI) */
    char *server_cmd; /* Command used to start server */
    pid_t pid;        /* LSP server process id */
    int to_fd;        /* write end (editor -> server stdin) */
    int from_fd;      /* read end (server stdout -> editor) */
    int initialized;  /* 1 after successful initialize/initialized */
    int next_id;      /* next JSON-RPC id for requests */

    /* Read buffer for incoming messages */
    char read_buf[LSP_READ_BUF_SIZE];
    int read_buf_len;

    /* Partial message state */
    int content_length; /* Expected content length, -1 if not reading body */
    char *msg_body;     /* Accumulated message body */
    int msg_body_len;   /* Current body length */
};

static LspServer *g_servers[LSP_MAX_SERVERS];
static int g_servers_count = 0;

/* Forward declarations */
static LspServer *lsp_server_for_lang(const char *lang);
static LspServer *lsp_server_create(const char *lang, const char *cmd,
                                     const char *root_uri);
static void lsp_server_destroy(LspServer *srv);
static void lsp_send_request(LspServer *srv, const char *method, cJSON *params,
                              int request_id);
static void lsp_send_notification(LspServer *srv, const char *method,
                                   cJSON *params);
static void lsp_handle_message(LspServer *srv, const char *msg, int len);
static void lsp_process_response(LspServer *srv, cJSON *json);
static void lsp_process_notification(LspServer *srv, cJSON *json);
static char *lsp_get_file_uri(const char *filepath);
static int lsp_start_server(LspServer *srv);
static void lsp_notify_existing_buffers(LspServer *srv);

/* Initialize LSP subsystem */
void lsp_init(void) {
    for (int i = 0; i < LSP_MAX_SERVERS; i++) {
        g_servers[i] = NULL;
    }
    g_servers_count = 0;
    log_msg("LSP: Initialized");
}

/* Shutdown all LSP servers */
void lsp_shutdown(void) {
    for (int i = 0; i < LSP_MAX_SERVERS; i++) {
        if (g_servers[i]) {
            lsp_server_destroy(g_servers[i]);
            g_servers[i] = NULL;
        }
    }
    g_servers_count = 0;
    log_msg("LSP: Shutdown complete");
}

/* Find server for a given language */
static LspServer *lsp_server_for_lang(const char *lang) {
    if (!lang)
        return NULL;
    for (int i = 0; i < LSP_MAX_SERVERS; i++) {
        if (g_servers[i] && g_servers[i]->lang &&
            strcmp(g_servers[i]->lang, lang) == 0) {
            return g_servers[i];
        }
    }
    return NULL;
}

/* Find server for a given buffer */
static LspServer *lsp_server_for_buffer(Buffer *buf) {
    if (!buf || !buf->filetype)
        return NULL;
    return lsp_server_for_lang(buf->filetype);
}

/* Create a new LSP server instance */
static LspServer *lsp_server_create(const char *lang, const char *cmd,
                                     const char *root_uri) {
    if (g_servers_count >= LSP_MAX_SERVERS) {
        log_msg("LSP: Maximum number of servers reached");
        return NULL;
    }

    LspServer *srv = calloc(1, sizeof(LspServer));
    if (!srv)
        return NULL;

    srv->lang = strdup(lang);
    srv->server_cmd = strdup(cmd);
    srv->root_uri = strdup(root_uri ? root_uri : "file:///");
    srv->pid = -1;
    srv->to_fd = -1;
    srv->from_fd = -1;
    srv->initialized = 0;
    srv->next_id = 1;
    srv->read_buf_len = 0;
    srv->content_length = -1;
    srv->msg_body = NULL;
    srv->msg_body_len = 0;

    /* Add to global array */
    for (int i = 0; i < LSP_MAX_SERVERS; i++) {
        if (!g_servers[i]) {
            g_servers[i] = srv;
            g_servers_count++;
            break;
        }
    }

    log_msg("LSP: Created server for lang=%s cmd=%s", lang, cmd);
    return srv;
}

/* Destroy LSP server and clean up resources */
static void lsp_server_destroy(LspServer *srv) {
    if (!srv)
        return;

    log_msg("LSP: Destroying server lang=%s pid=%d", srv->lang ? srv->lang : "?",
            srv->pid);

    /* Close file descriptors */
    if (srv->to_fd >= 0) {
        close(srv->to_fd);
        srv->to_fd = -1;
    }
    if (srv->from_fd >= 0) {
        close(srv->from_fd);
        srv->from_fd = -1;
    }

    /* Terminate server process */
    if (srv->pid > 0) {
        /* Closing stdin/stdout will cause LSP server to exit */
        /* Wait briefly for graceful shutdown */
        int status;
        waitpid(srv->pid, &status, WNOHANG);
        srv->pid = -1;
    }

    /* Free allocated memory */
    free(srv->lang);
    free(srv->root_uri);
    free(srv->server_cmd);
    free(srv->msg_body);
    free(srv);

    /* Remove from global array */
    for (int i = 0; i < LSP_MAX_SERVERS; i++) {
        if (g_servers[i] == srv) {
            g_servers[i] = NULL;
            g_servers_count--;
            break;
        }
    }
}

/* Start LSP server process */
static int lsp_start_server(LspServer *srv) {
    if (!srv || !srv->server_cmd)
        return -1;

    int to_server[2];   /* editor writes, server reads (stdin) */
    int from_server[2]; /* server writes, editor reads (stdout) */

    if (pipe(to_server) < 0 || pipe(from_server) < 0) {
        log_msg("LSP: Failed to create pipes: %s", strerror(errno));
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        log_msg("LSP: Fork failed: %s", strerror(errno));
        close(to_server[0]);
        close(to_server[1]);
        close(from_server[0]);
        close(from_server[1]);
        return -1;
    }

    if (pid == 0) {
        /* Child process - setup and exec LSP server */
        close(to_server[1]);   /* Close write end in child */
        close(from_server[0]); /* Close read end in child */

        /* Redirect stdin/stdout to pipes */
        dup2(to_server[0], STDIN_FILENO);
        dup2(from_server[1], STDOUT_FILENO);

        /* Close stderr or redirect to /dev/null */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        close(to_server[0]);
        close(from_server[1]);

        /* Parse command and exec */
        /* Simple parsing - splits on spaces */
        char *cmd_copy = strdup(srv->server_cmd);
        char *args[32];
        int argc = 0;
        char *token = strtok(cmd_copy, " ");
        while (token && argc < 31) {
            args[argc++] = token;
            token = strtok(NULL, " ");
        }
        args[argc] = NULL;

        execvp(args[0], args);
        /* If exec fails */
        fprintf(stderr, "LSP: Failed to exec %s: %s\n", args[0], strerror(errno));
        exit(1);
    }

    /* Parent process */
    close(to_server[0]);   /* Close read end in parent */
    close(from_server[1]); /* Close write end in parent */

    srv->pid = pid;
    srv->to_fd = to_server[1];
    srv->from_fd = from_server[0];

    /* Set non-blocking mode on read fd */
    int flags = fcntl(srv->from_fd, F_GETFL, 0);
    fcntl(srv->from_fd, F_SETFL, flags | O_NONBLOCK);

    log_msg("LSP: Started server pid=%d cmd=%s", pid, srv->server_cmd);
    return 0;
}

/* Get file:// URI for a filepath */
static char *lsp_get_file_uri(const char *filepath) {
    if (!filepath)
        return NULL;

    /* Simple URI construction - handle absolute paths */
    size_t len = strlen(filepath);
    char *uri = malloc(len + 32);
    if (!uri)
        return NULL;

    if (filepath[0] == '/') {
        sprintf(uri, "file://%s", filepath);
    } else {
        /* Relative path - prefix with cwd */
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd))) {
            sprintf(uri, "file://%s/%s", cwd, filepath);
        } else {
            sprintf(uri, "file://%s", filepath);
        }
    }
    return uri;
}

/* Send initialize request to LSP server */
static void lsp_send_initialize(LspServer *srv) {
    if (!srv)
        return;

    cJSON *params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "processId", (double)getpid());
    cJSON_AddStringToObject(params, "rootUri", srv->root_uri);

    cJSON *capabilities = cJSON_CreateObject();
    cJSON *textDocument = cJSON_CreateObject();
    cJSON_AddItemToObject(capabilities, "textDocument", textDocument);
    cJSON_AddItemToObject(params, "capabilities", capabilities);

    int req_id = srv->next_id++;
    lsp_send_request(srv, "initialize", params, req_id);
    log_msg("LSP: Sent initialize request id=%d", req_id);
}

/* Send initialized notification */
static void lsp_send_initialized(LspServer *srv) {
    if (!srv)
        return;
    cJSON *params = cJSON_CreateObject();
    lsp_send_notification(srv, "initialized", params);
    srv->initialized = 1;
    log_msg("LSP: Sent initialized notification");
}

/* Send JSON-RPC request */
static void lsp_send_request(LspServer *srv, const char *method, cJSON *params,
                              int request_id) {
    if (!srv || srv->to_fd < 0)
        return;

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(req, "id", request_id);
    cJSON_AddStringToObject(req, "method", method);
    if (params)
        cJSON_AddItemToObject(req, "params", params);

    char *json_str = cJSON_PrintUnformatted(req);
    if (!json_str) {
        cJSON_Delete(req);
        return;
    }

    /* LSP uses Content-Length framing */
    char header[128];
    int content_len = strlen(json_str);
    snprintf(header, sizeof(header), "Content-Length: %d\r\n\r\n", content_len);

    /* Write header + body */
    write(srv->to_fd, header, strlen(header));
    write(srv->to_fd, json_str, content_len);

    log_msg("LSP: Sent request method=%s id=%d len=%d", method, request_id,
            content_len);

    free(json_str);
    cJSON_Delete(req);
}

/* Send JSON-RPC notification */
static void lsp_send_notification(LspServer *srv, const char *method,
                                   cJSON *params) {
    if (!srv || srv->to_fd < 0)
        return;

    cJSON *notif = cJSON_CreateObject();
    cJSON_AddStringToObject(notif, "jsonrpc", "2.0");
    cJSON_AddStringToObject(notif, "method", method);
    if (params)
        cJSON_AddItemToObject(notif, "params", params);

    char *json_str = cJSON_PrintUnformatted(notif);
    if (!json_str) {
        cJSON_Delete(notif);
        return;
    }

    char header[128];
    int content_len = strlen(json_str);
    snprintf(header, sizeof(header), "Content-Length: %d\r\n\r\n", content_len);

    write(srv->to_fd, header, strlen(header));
    write(srv->to_fd, json_str, content_len);

    log_msg("LSP: Sent notification method=%s len=%d", method, content_len);

    free(json_str);
    cJSON_Delete(notif);
}

/* Add LSP file descriptors to select() set */
void lsp_fill_fdset(fd_set *set, int *max_fd) {
    for (int i = 0; i < LSP_MAX_SERVERS; i++) {
        if (g_servers[i] && g_servers[i]->from_fd >= 0) {
            FD_SET(g_servers[i]->from_fd, set);
            if (g_servers[i]->from_fd > *max_fd) {
                *max_fd = g_servers[i]->from_fd;
            }
        }
    }
}

/* Handle readable LSP file descriptors */
void lsp_handle_readable(const fd_set *set) {
    for (int i = 0; i < LSP_MAX_SERVERS; i++) {
        LspServer *srv = g_servers[i];
        if (!srv || srv->from_fd < 0)
            continue;

        if (FD_ISSET(srv->from_fd, set)) {
            /* Read available data */
            int space = LSP_READ_BUF_SIZE - srv->read_buf_len;
            if (space <= 0) {
                log_msg("LSP: Read buffer full, discarding");
                srv->read_buf_len = 0;
                continue;
            }

            ssize_t n =
                read(srv->from_fd, srv->read_buf + srv->read_buf_len, space);
            if (n <= 0) {
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                    continue;
                log_msg("LSP: Server disconnected (read=%zd errno=%d)", n, errno);
                lsp_server_destroy(srv);
                g_servers[i] = NULL;
                continue;
            }

            srv->read_buf_len += n;

            /* Process complete messages in buffer */
            while (srv->read_buf_len > 0) {
                if (srv->content_length < 0) {
                    /* Looking for Content-Length header */
                    char *header_end = strstr(srv->read_buf, "\r\n\r\n");
                    if (!header_end)
                        break; /* Need more data */

                    /* Parse Content-Length */
                    char *cl = strstr(srv->read_buf, "Content-Length:");
                    if (!cl || cl > header_end) {
                        log_msg("LSP: Invalid header, no Content-Length");
                        srv->read_buf_len = 0;
                        break;
                    }

                    srv->content_length = atoi(cl + 15);
                    int header_len = (header_end - srv->read_buf) + 4;

                    /* Remove header from buffer */
                    memmove(srv->read_buf, header_end + 4,
                            srv->read_buf_len - header_len);
                    srv->read_buf_len -= header_len;

                    /* Allocate body buffer */
                    srv->msg_body = malloc(srv->content_length + 1);
                    srv->msg_body_len = 0;
                }

                /* Reading message body */
                if (srv->content_length >= 0) {
                    int needed = srv->content_length - srv->msg_body_len;
                    int available = srv->read_buf_len;
                    int to_copy = needed < available ? needed : available;

                    memcpy(srv->msg_body + srv->msg_body_len, srv->read_buf,
                           to_copy);
                    srv->msg_body_len += to_copy;

                    /* Remove copied data from read buffer */
                    memmove(srv->read_buf, srv->read_buf + to_copy,
                            srv->read_buf_len - to_copy);
                    srv->read_buf_len -= to_copy;

                    /* Check if message is complete */
                    if (srv->msg_body_len >= srv->content_length) {
                        srv->msg_body[srv->content_length] = '\0';
                        lsp_handle_message(srv, srv->msg_body, srv->content_length);

                        free(srv->msg_body);
                        srv->msg_body = NULL;
                        srv->msg_body_len = 0;
                        srv->content_length = -1;
                    }
                }

                if (srv->content_length < 0 && srv->read_buf_len == 0)
                    break;
            }
        }
    }
}

/* Handle complete LSP message */
static void lsp_handle_message(LspServer *srv, const char *msg, int len) {
    log_msg("LSP: Received message len=%d: %.200s", len, msg);

    cJSON *json = json_parse(msg, len);
    if (!json) {
        log_msg("LSP: Failed to parse JSON");
        return;
    }

    /* Check if it's a response or notification */
    cJSON *id = cJSON_GetObjectItemCaseSensitive(json, "id");
    if (id) {
        lsp_process_response(srv, json);
    } else {
        lsp_process_notification(srv, json);
    }

    cJSON_Delete(json);
}

/* Process LSP response */
static void lsp_process_response(LspServer *srv, cJSON *json) {
    cJSON *result = cJSON_GetObjectItemCaseSensitive(json, "result");
    cJSON *error = cJSON_GetObjectItemCaseSensitive(json, "error");

    if (error) {
        const char *err_msg = json_get_string(error, "message");
        log_msg("LSP: Response error: %s", err_msg ? err_msg : "unknown");
        ed_set_status_message("LSP error: %s", err_msg ? err_msg : "unknown");
        return;
    }

    /* Handle initialize response */
    if (!srv->initialized && result) {
        log_msg("LSP: Received initialize response");
        lsp_send_initialized(srv);

        /* Send didOpen for any already-open buffers matching this language */
        lsp_notify_existing_buffers(srv);
    }
}

/* Process LSP notification */
static void lsp_process_notification(LspServer *srv, cJSON *json) {
    (void)srv; /* May be used in future for server-specific handling */

    const char *method = json_get_string(json, "method");
    if (!method)
        return;

    log_msg("LSP: Notification: %s", method);

    if (strcmp(method, "textDocument/publishDiagnostics") == 0) {
        /* TODO: Handle diagnostics */
        cJSON *params = json_get_object(json, "params");
        if (params) {
            const char *uri = json_get_string(params, "uri");
            log_msg("LSP: Diagnostics for %s", uri ? uri : "?");
        }
    }
}

/* Buffer opened - notify LSP */
void lsp_on_buffer_open(Buffer *buf) {
    if (!buf || !buf->filename || !buf->filetype)
        return;

    LspServer *srv = lsp_server_for_buffer(buf);
    if (!srv || !srv->initialized)
        return;

    char *uri = lsp_get_file_uri(buf->filename);
    if (!uri)
        return;

    /* Read file content */
    FILE *f = fopen(buf->filename, "r");
    if (!f) {
        free(uri);
        return;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *content = malloc(size + 1);
    if (!content) {
        fclose(f);
        free(uri);
        return;
    }

    fread(content, 1, size, f);
    content[size] = '\0';
    fclose(f);

    /* Send textDocument/didOpen */
    cJSON *params = cJSON_CreateObject();
    cJSON *textDocument = cJSON_CreateObject();
    cJSON_AddStringToObject(textDocument, "uri", uri);
    cJSON_AddStringToObject(textDocument, "languageId", buf->filetype);
    cJSON_AddNumberToObject(textDocument, "version", 1);
    cJSON_AddStringToObject(textDocument, "text", content);
    cJSON_AddItemToObject(params, "textDocument", textDocument);

    lsp_send_notification(srv, "textDocument/didOpen", params);

    free(content);
    free(uri);
    log_msg("LSP: Sent didOpen for %s", buf->filename);
}

/* Buffer closed - notify LSP */
void lsp_on_buffer_close(Buffer *buf) {
    if (!buf || !buf->filename)
        return;

    LspServer *srv = lsp_server_for_buffer(buf);
    if (!srv || !srv->initialized)
        return;

    char *uri = lsp_get_file_uri(buf->filename);
    if (!uri)
        return;

    cJSON *params = cJSON_CreateObject();
    cJSON *textDocument = cJSON_CreateObject();
    cJSON_AddStringToObject(textDocument, "uri", uri);
    cJSON_AddItemToObject(params, "textDocument", textDocument);

    lsp_send_notification(srv, "textDocument/didClose", params);

    free(uri);
    log_msg("LSP: Sent didClose for %s", buf->filename);
}

/* Buffer saved - notify LSP */
void lsp_on_buffer_save(Buffer *buf) {
    if (!buf || !buf->filename)
        return;

    LspServer *srv = lsp_server_for_buffer(buf);
    if (!srv || !srv->initialized)
        return;

    char *uri = lsp_get_file_uri(buf->filename);
    if (!uri)
        return;

    cJSON *params = cJSON_CreateObject();
    cJSON *textDocument = cJSON_CreateObject();
    cJSON_AddStringToObject(textDocument, "uri", uri);
    cJSON_AddItemToObject(params, "textDocument", textDocument);

    lsp_send_notification(srv, "textDocument/didSave", params);

    free(uri);
    log_msg("LSP: Sent didSave for %s", buf->filename);
}

/* Buffer changed - notify LSP */
void lsp_on_buffer_changed(Buffer *buf) {
    /* TODO: Implement incremental sync with textDocument/didChange */
    /* For now, we skip this to avoid flooding the server */
    (void)buf;
}

/* Request hover information */
void lsp_request_hover(Buffer *buf, int line, int col) {
    if (!buf || !buf->filename)
        return;

    LspServer *srv = lsp_server_for_buffer(buf);
    if (!srv || !srv->initialized) {
        ed_set_status_message("LSP: No server for %s",
                               buf->filetype ? buf->filetype : "unknown");
        return;
    }

    char *uri = lsp_get_file_uri(buf->filename);
    if (!uri)
        return;

    cJSON *params = cJSON_CreateObject();
    cJSON *textDocument = cJSON_CreateObject();
    cJSON_AddStringToObject(textDocument, "uri", uri);
    cJSON_AddItemToObject(params, "textDocument", textDocument);

    cJSON *position = cJSON_CreateObject();
    cJSON_AddNumberToObject(position, "line", line);
    cJSON_AddNumberToObject(position, "character", col);
    cJSON_AddItemToObject(params, "position", position);

    int req_id = srv->next_id++;
    lsp_send_request(srv, "textDocument/hover", params, req_id);

    free(uri);
    ed_set_status_message("LSP: Requesting hover...");
}

/* Request go to definition */
void lsp_request_definition(Buffer *buf, int line, int col) {
    if (!buf || !buf->filename)
        return;

    LspServer *srv = lsp_server_for_buffer(buf);
    if (!srv || !srv->initialized) {
        ed_set_status_message("LSP: No server for %s",
                               buf->filetype ? buf->filetype : "unknown");
        return;
    }

    char *uri = lsp_get_file_uri(buf->filename);
    if (!uri)
        return;

    cJSON *params = cJSON_CreateObject();
    cJSON *textDocument = cJSON_CreateObject();
    cJSON_AddStringToObject(textDocument, "uri", uri);
    cJSON_AddItemToObject(params, "textDocument", textDocument);

    cJSON *position = cJSON_CreateObject();
    cJSON_AddNumberToObject(position, "line", line);
    cJSON_AddNumberToObject(position, "character", col);
    cJSON_AddItemToObject(params, "position", position);

    int req_id = srv->next_id++;
    lsp_send_request(srv, "textDocument/definition", params, req_id);

    free(uri);
    ed_set_status_message("LSP: Requesting definition...");
}

/* Request completion */
void lsp_request_completion(Buffer *buf, int line, int col) {
    if (!buf || !buf->filename)
        return;

    LspServer *srv = lsp_server_for_buffer(buf);
    if (!srv || !srv->initialized) {
        ed_set_status_message("LSP: No server for %s",
                               buf->filetype ? buf->filetype : "unknown");
        return;
    }

    char *uri = lsp_get_file_uri(buf->filename);
    if (!uri)
        return;

    cJSON *params = cJSON_CreateObject();
    cJSON *textDocument = cJSON_CreateObject();
    cJSON_AddStringToObject(textDocument, "uri", uri);
    cJSON_AddItemToObject(params, "textDocument", textDocument);

    cJSON *position = cJSON_CreateObject();
    cJSON_AddNumberToObject(position, "line", line);
    cJSON_AddNumberToObject(position, "character", col);
    cJSON_AddItemToObject(params, "position", position);

    int req_id = srv->next_id++;
    lsp_send_request(srv, "textDocument/completion", params, req_id);

    free(uri);
    ed_set_status_message("LSP: Requesting completion...");
}

/* Send didOpen for all currently open buffers matching a language */
static void lsp_notify_existing_buffers(LspServer *srv) {
    if (!srv || !srv->initialized)
        return;

    /* Iterate through all open buffers */
    for (size_t i = 0; i < E.buffers.len; i++) {
        Buffer *buf = &E.buffers.data[i];

        /* Check if buffer matches this server's language */
        if (buf && buf->filetype && buf->filename &&
            strcmp(buf->filetype, srv->lang) == 0) {
            log_msg("LSP: Sending didOpen for existing buffer: %s", buf->filename);
            lsp_on_buffer_open(buf);
        }
    }
}

/* Command to start an LSP server for a language */
int lsp_cmd_start(const char *lang, const char *cmd, const char *root_uri) {
    if (!lang || !cmd) {
        ed_set_status_message("LSP: Usage: lsp_start <lang> <command>");
        return -1;
    }

    /* Check if server already exists */
    LspServer *existing = lsp_server_for_lang(lang);
    if (existing) {
        ed_set_status_message("LSP: Server for %s already running", lang);
        return 0;
    }

    /* Create server */
    LspServer *srv = lsp_server_create(lang, cmd, root_uri);
    if (!srv) {
        ed_set_status_message("LSP: Failed to create server for %s", lang);
        return -1;
    }

    /* Start server process */
    if (lsp_start_server(srv) < 0) {
        ed_set_status_message("LSP: Failed to start server for %s", lang);
        lsp_server_destroy(srv);
        return -1;
    }

    /* Send initialize request */
    lsp_send_initialize(srv);

    ed_set_status_message("LSP: Started server for %s (pid=%d)", lang, srv->pid);
    return 0;
}

/* Command to stop LSP server for a language */
int lsp_cmd_stop(const char *lang) {
    if (!lang) {
        ed_set_status_message("LSP: Usage: lsp_stop <lang>");
        return -1;
    }

    LspServer *srv = lsp_server_for_lang(lang);
    if (!srv) {
        ed_set_status_message("LSP: No server for %s", lang);
        return -1;
    }

    ed_set_status_message("LSP: Stopping server for %s", lang);
    lsp_server_destroy(srv);

    /* Remove from array */
    for (int i = 0; i < LSP_MAX_SERVERS; i++) {
        if (g_servers[i] == srv) {
            g_servers[i] = NULL;
            break;
        }
    }

    return 0;
}
