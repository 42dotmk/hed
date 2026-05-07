/* copilot_proto.c — spawn the @github/copilot-language-server child,
 * frame JSON-RPC messages over its stdio, dispatch incoming messages
 * to copilot.c::cp_handle_message().
 *
 * Mirrors the framing parser from plugins/lsp/lsp_impl.c (Content-Length
 * headers, single read buffer, per-message body accumulator). */

#include "hed.h"
#include "select_loop.h"
#include "copilot_internal.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>

Copilot CP;

/* --- locate the language-server -----------------------------------
 *
 * Resolution order:
 *   1. $HED_COPILOT_LSP (explicit override; may be a binary or a .js).
 *   2. `copilot-language-server` in PATH — what `npm i -g
 *      @github/copilot-language-server` installs. The npm shim is a
 *      shebang'd JS file, so execlp-via-PATH handles it directly.
 *   3. ./node_modules/@github/copilot-language-server/dist/language-server.js
 *      relative to the editor cwd, for projects with a local install.
 *
 * The result is two strings: argv0 to exec and an optional second token
 * (used when we need to invoke `node <path>` for a bare .js file). */

typedef struct {
    char *argv0;     /* what to execlp / execvp */
    char *script;    /* if non-NULL, prepend "node" — i.e. argv = node script --stdio */
} CpServerInvocation;

static const char *cp_lsp_relative =
    "node_modules/@github/copilot-language-server/dist/language-server.js";

static int has_js_suffix(const char *s) {
    size_t n = strlen(s);
    return n >= 3 && strcmp(s + n - 3, ".js") == 0;
}

/* Search $PATH for an executable. Returns malloc'd absolute path on
 * success, NULL otherwise. */
static char *find_in_path(const char *name) {
    const char *path = getenv("PATH");
    if (!path || !*path) return NULL;
    size_t name_len = strlen(name);
    const char *p = path;
    while (*p) {
        const char *colon = strchr(p, ':');
        size_t      seg   = colon ? (size_t)(colon - p) : strlen(p);
        if (seg > 0) {
            size_t n   = seg + 1 + name_len + 1;
            char  *cand = malloc(n);
            if (cand) {
                memcpy(cand, p, seg);
                cand[seg] = '/';
                memcpy(cand + seg + 1, name, name_len);
                cand[seg + 1 + name_len] = '\0';
                if (access(cand, X_OK) == 0) return cand;
                free(cand);
            }
        }
        if (!colon) break;
        p = colon + 1;
    }
    return NULL;
}

static int cp_resolve_invocation(CpServerInvocation *out) {
    out->argv0 = NULL;
    out->script = NULL;

    /* 1. explicit override */
    const char *env = getenv("HED_COPILOT_LSP");
    if (env && *env && access(env, R_OK) == 0) {
        if (has_js_suffix(env)) {
            out->argv0  = strdup("node");
            out->script = strdup(env);
        } else {
            out->argv0  = strdup(env);
        }
        return 0;
    }

    /* 2. global install — `copilot-language-server` in PATH. */
    char *bin = find_in_path("copilot-language-server");
    if (bin) {
        out->argv0 = bin;            /* take ownership */
        return 0;
    }

    /* 3. cwd-relative local install. */
    if (access(cp_lsp_relative, R_OK) == 0) {
        out->argv0  = strdup("node");
        out->script = strdup(cp_lsp_relative);
        return 0;
    }
    if (E.cwd[0]) {
        size_t n = strlen(E.cwd) + 1 + strlen(cp_lsp_relative) + 1;
        char  *p = malloc(n);
        if (p) {
            snprintf(p, n, "%s/%s", E.cwd, cp_lsp_relative);
            if (access(p, R_OK) == 0) {
                out->argv0  = strdup("node");
                out->script = p;
                return 0;
            }
            free(p);
        }
    }

    return -1;
}

static void cp_invocation_free(CpServerInvocation *inv) {
    free(inv->argv0);
    free(inv->script);
    inv->argv0 = inv->script = NULL;
}

/* --- forward decls ------------------------------------------------- */

static void cp_on_readable(int fd, void *ud);
static void cp_pending_add(int id, CpReqKind kind);

/* --- spawn --------------------------------------------------------- */

int cp_proto_spawn(void) {
    if (CP.spawned) {
        ed_set_status_message("copilot: already running");
        return 0;
    }

    CpServerInvocation inv;
    if (cp_resolve_invocation(&inv) != 0) {
        ed_set_status_message(
            "copilot: language server not found "
            "(install with `npm i -g @github/copilot-language-server` "
            "or set HED_COPILOT_LSP)");
        return -1;
    }

    int in_pipe[2];   /* parent -> child stdin  */
    int out_pipe[2];  /* child stdout -> parent */
    if (pipe(in_pipe) < 0 || pipe(out_pipe) < 0) {
        ed_set_status_message("copilot: pipe() failed: %s", strerror(errno));
        cp_invocation_free(&inv);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        ed_set_status_message("copilot: fork() failed: %s", strerror(errno));
        close(in_pipe[0]);  close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        cp_invocation_free(&inv);
        return -1;
    }

    if (pid == 0) {
        /* Child: hook up stdin/stdout. Send stderr to the editor log
         * so node/copilot's noise (deprecation warnings, telemetry
         * complaints, etc.) doesn't repaint over the renderer. */
        dup2(in_pipe[0],  STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        int log_fd = log_fileno();
        if (log_fd >= 0) dup2(log_fd, STDERR_FILENO);
        close(in_pipe[0]);  close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);

        if (inv.script) {
            /* node <script> --stdio */
            execlp(inv.argv0, inv.argv0, inv.script, "--stdio", (char *)NULL);
        } else {
            /* <argv0> --stdio (binary in PATH) */
            execlp(inv.argv0, inv.argv0, "--stdio", (char *)NULL);
        }
        /* If exec returns, write the error directly to the log fd
         * (parent's stderr is the user's terminal — we don't want to
         * paint there). */
        char err[256];
        int  n = snprintf(err, sizeof(err),
                          "copilot: execlp(%s) failed: %s\n",
                          inv.argv0, strerror(errno));
        if (n > 0 && log_fd >= 0) (void)write(log_fd, err, (size_t)n);
        _exit(127);
    }

    /* Parent: keep the write end of stdin and read end of stdout. */
    close(in_pipe[0]);
    close(out_pipe[1]);

    int  to_fd   = in_pipe[1];
    int  from_fd = out_pipe[0];
    int  flags   = fcntl(from_fd, F_GETFL, 0);
    if (flags >= 0) fcntl(from_fd, F_SETFL, flags | O_NONBLOCK);

    CP.pid             = pid;
    CP.to_fd           = to_fd;
    CP.from_fd         = from_fd;
    CP.spawned         = 1;
    CP.initialized     = 0;
    CP.next_id         = 1;
    CP.read_buf_len    = 0;
    CP.content_length  = -1;
    CP.msg_body        = NULL;
    CP.msg_body_len    = 0;

    ed_loop_register("copilot", from_fd, cp_on_readable, NULL);

    log_msg("copilot: spawned %s%s%s pid=%d (in=%d out=%d)",
            inv.argv0,
            inv.script ? " " : "",
            inv.script ? inv.script : "",
            pid, to_fd, from_fd);
    cp_invocation_free(&inv);
    return 0;
}

void cp_proto_shutdown(void) {
    if (!CP.spawned) return;

    if (CP.from_fd >= 0) {
        ed_loop_unregister(CP.from_fd);
        close(CP.from_fd);
        CP.from_fd = -1;
    }
    if (CP.to_fd >= 0) {
        close(CP.to_fd);
        CP.to_fd = -1;
    }
    if (CP.pid > 0) {
        kill(CP.pid, SIGTERM);
        /* Don't block on waitpid; let SIGCHLD reap it. If the user has
         * SIGCHLD ignored (default), the kernel auto-reaps. */
        waitpid(CP.pid, NULL, WNOHANG);
        CP.pid = 0;
    }

    free(CP.msg_body);
    CP.msg_body       = NULL;
    CP.msg_body_len   = 0;
    CP.content_length = -1;
    CP.read_buf_len   = 0;
    CP.spawned        = 0;
    CP.initialized    = 0;

    for (int i = 0; i < CP_PENDING_MAX; i++) CP.pending[i].kind = CP_REQ_NONE;
}

/* --- pending table ------------------------------------------------- */

static void cp_pending_add(int id, CpReqKind kind) {
    for (int i = 0; i < CP_PENDING_MAX; i++) {
        if (CP.pending[i].kind == CP_REQ_NONE) {
            CP.pending[i].id   = id;
            CP.pending[i].kind = kind;
            return;
        }
    }
    log_msg("copilot: pending table full, dropping id=%d", id);
}

CpReqKind cp_proto_pending_pop(int id) {
    for (int i = 0; i < CP_PENDING_MAX; i++) {
        if (CP.pending[i].kind != CP_REQ_NONE && CP.pending[i].id == id) {
            CpReqKind k = CP.pending[i].kind;
            CP.pending[i].kind = CP_REQ_NONE;
            return k;
        }
    }
    return CP_REQ_NONE;
}

/* --- send ---------------------------------------------------------- */

static void cp_send_raw(const char *body) {
    if (!CP.spawned || CP.to_fd < 0) return;
    char header[64];
    int  clen = (int)strlen(body);
    int  hlen = snprintf(header, sizeof(header),
                         "Content-Length: %d\r\n\r\n", clen);
    write(CP.to_fd, header, (size_t)hlen);
    write(CP.to_fd, body,   (size_t)clen);
}

int cp_proto_request(const char *method, cJSON *params, CpReqKind kind) {
    if (!CP.spawned) {
        if (params) cJSON_Delete(params);
        return -1;
    }
    int id = CP.next_id++;

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(req, "id", id);
    cJSON_AddStringToObject(req, "method", method);
    if (params) cJSON_AddItemToObject(req, "params", params);
    char *s = cJSON_PrintUnformatted(req);
    if (s) { cp_send_raw(s); free(s); }
    cJSON_Delete(req);

    cp_pending_add(id, kind);
    log_msg("copilot: -> %s id=%d", method, id);
    return id;
}

void cp_proto_notify(const char *method, cJSON *params) {
    if (!CP.spawned) {
        if (params) cJSON_Delete(params);
        return;
    }
    cJSON *notif = cJSON_CreateObject();
    cJSON_AddStringToObject(notif, "jsonrpc", "2.0");
    cJSON_AddStringToObject(notif, "method", method);
    if (params) cJSON_AddItemToObject(notif, "params", params);
    char *s = cJSON_PrintUnformatted(notif);
    if (s) { cp_send_raw(s); free(s); }
    cJSON_Delete(notif);
    log_msg("copilot: -> %s (notification)", method);
}

/* --- recv ---------------------------------------------------------- */

static void cp_on_readable(int fd, void *ud) {
    (void)ud;
    if (fd != CP.from_fd) return;

    int space = CP_READ_BUF_SIZE - CP.read_buf_len;
    if (space <= 0) { CP.read_buf_len = 0; return; }

    ssize_t n = read(CP.from_fd, CP.read_buf + CP.read_buf_len, (size_t)space);
    if (n <= 0) {
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        log_msg("copilot: child closed stdout");
        ed_set_status_message("copilot: server exited");
        cp_proto_shutdown();
        return;
    }
    CP.read_buf_len += (int)n;

    while (CP.read_buf_len > 0) {
        if (CP.content_length < 0) {
            char *hend = strstr(CP.read_buf, "\r\n\r\n");
            if (!hend) break;
            char *cl   = strstr(CP.read_buf, "Content-Length:");
            if (!cl || cl > hend) { CP.read_buf_len = 0; break; }
            CP.content_length = atoi(cl + 15);
            int hlen = (int)(hend - CP.read_buf) + 4;
            memmove(CP.read_buf, hend + 4,
                    (size_t)(CP.read_buf_len - hlen));
            CP.read_buf_len -= hlen;
            free(CP.msg_body);
            CP.msg_body     = malloc((size_t)CP.content_length + 1);
            CP.msg_body_len = 0;
        }

        if (CP.content_length >= 0 && CP.msg_body) {
            int need  = CP.content_length - CP.msg_body_len;
            int avail = CP.read_buf_len;
            int copy  = need < avail ? need : avail;
            memcpy(CP.msg_body + CP.msg_body_len, CP.read_buf, (size_t)copy);
            CP.msg_body_len += copy;
            memmove(CP.read_buf, CP.read_buf + copy,
                    (size_t)(CP.read_buf_len - copy));
            CP.read_buf_len -= copy;

            if (CP.msg_body_len >= CP.content_length) {
                CP.msg_body[CP.content_length] = '\0';
                cp_handle_message(CP.msg_body, CP.content_length);
                free(CP.msg_body);
                CP.msg_body       = NULL;
                CP.msg_body_len   = 0;
                CP.content_length = -1;
            }
        }

        if (CP.content_length < 0 && CP.read_buf_len == 0) break;
        if (CP.content_length >= 0 && CP.read_buf_len == 0) break;
    }
}
