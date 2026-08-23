/* copilot_proto.c — spawn the @github/copilot-language-server child,
 * frame JSON-RPC messages over its stdio, dispatch incoming messages
 * to copilot.c::cp_handle_message().
 *
 * Mirrors the framing parser from plugins/lsp/lsp_impl.c (Content-Length
 * headers, single read buffer, per-message body accumulator). */

#include "copilot_internal.h"
#include "hed.h"
#include "select_loop.h"

#include <fcntl.h>
#include <poll.h>
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
    char *argv0;  /* what to execlp / execvp */
    char *script; /* if non-NULL, prepend "node" — i.e. argv = node script
                     --stdio */
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
    if (!path || !*path)
        return NULL;
    size_t name_len = strlen(name);
    const char *p = path;
    while (*p) {
        const char *colon = strchr(p, ':');
        size_t seg = colon ? (size_t)(colon - p) : strlen(p);
        if (seg > 0) {
            size_t n = seg + 1 + name_len + 1;
            char *cand = malloc(n);
            if (cand) {
                memcpy(cand, p, seg);
                cand[seg] = '/';
                memcpy(cand + seg + 1, name, name_len);
                cand[seg + 1 + name_len] = '\0';
                if (fs_is_executable(cand))
                    return cand;
                free(cand);
            }
        }
        if (!colon)
            break;
        p = colon + 1;
    }
    return NULL;
}

static int cp_resolve_invocation(CpServerInvocation *out) {
    out->argv0 = NULL;
    out->script = NULL;

    /* 1. explicit override */
    const char *env = getenv("HED_COPILOT_LSP");
    if (env && *env && fs_is_file(env)) {
        if (has_js_suffix(env)) {
            out->argv0 = strdup("node");
            out->script = strdup(env);
        } else {
            out->argv0 = strdup(env);
        }
        return 0;
    }

    /* 2. global install — `copilot-language-server` in PATH. */
    char *bin = find_in_path("copilot-language-server");
    if (bin) {
        out->argv0 = bin; /* take ownership */
        return 0;
    }

    /* 3. cwd-relative local install. */
    if (fs_is_file(cp_lsp_relative)) {
        out->argv0 = strdup("node");
        out->script = strdup(cp_lsp_relative);
        return 0;
    }
    if (E.cwd[0]) {
        size_t n = strlen(E.cwd) + 1 + strlen(cp_lsp_relative) + 1;
        char *p = malloc(n);
        if (p) {
            snprintf(p, n, "%s/%s", E.cwd, cp_lsp_relative);
            if (fs_is_file(p)) {
                out->argv0 = strdup("node");
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

    /* stderr goes to the editor log (proc_spawn's default) so node/
     * copilot noise (deprecation warnings, telemetry complaints, ...)
     * doesn't repaint over the renderer. */
    const char *argv[4];
    int argc = 0;
    argv[argc++] = inv.argv0;
    if (inv.script)
        argv[argc++] = inv.script; /* node <script> --stdio */
    argv[argc++] = "--stdio";
    argv[argc] = NULL;

    Proc pr;
    if (proc_spawn(argv, PROC_STDIN, &pr) != 0) {
        ed_set_status_message("copilot: failed to spawn %s", inv.argv0);
        cp_invocation_free(&inv);
        return -1;
    }

    CP.pid = pr.pid;
    CP.to_fd = pr.to_fd;
    CP.from_fd = pr.from_fd;
    CP.spawned = 1;
    CP.initialized = 0;
    CP.next_id = 1;
    jrpc_reader_init(&CP.reader);

    ed_loop_register("copilot", CP.from_fd, cp_on_readable, NULL);

    log_msg("copilot: spawned %s%s%s pid=%d (in=%d out=%d)", inv.argv0,
            inv.script ? " " : "", inv.script ? inv.script : "", (int)CP.pid,
            CP.to_fd, CP.from_fd);
    cp_invocation_free(&inv);
    return 0;
}

void cp_proto_shutdown(void) {
    if (!CP.spawned)
        return;

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

    jrpc_reader_free(&CP.reader);
    CP.spawned = 0;
    CP.initialized = 0;

    for (int i = 0; i < CP_PENDING_MAX; i++)
        CP.pending[i].kind = CP_REQ_NONE;
}

/* --- pending table ------------------------------------------------- */

static void cp_pending_add(int id, CpReqKind kind) {
    for (int i = 0; i < CP_PENDING_MAX; i++) {
        if (CP.pending[i].kind == CP_REQ_NONE) {
            CP.pending[i].id = id;
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

int cp_proto_request(const char *method, cJSON *params, CpReqKind kind) {
    if (!CP.spawned) {
        if (params)
            cJSON_Delete(params);
        return -1;
    }
    int id = CP.next_id++;
    if (jrpc_send(CP.to_fd, jrpc_request(method, params, id)) < 0)
        log_msg("copilot: write failed: %s", strerror(errno));
    cp_pending_add(id, kind);
    log_msg("copilot: -> %s id=%d", method, id);
    return id;
}

void cp_proto_notify(const char *method, cJSON *params) {
    if (!CP.spawned) {
        if (params)
            cJSON_Delete(params);
        return;
    }
    if (jrpc_send(CP.to_fd, jrpc_notification(method, params)) < 0)
        log_msg("copilot: write failed: %s", strerror(errno));
    log_msg("copilot: -> %s (notification)", method);
}

/* --- recv ---------------------------------------------------------- */

static void cp_on_readable(int fd, void *ud) {
    (void)ud;
    if (fd != CP.from_fd)
        return;

    char tmp[65536];
    ssize_t n = read(CP.from_fd, tmp, sizeof(tmp));
    if (n <= 0) {
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return;
        log_msg("copilot: child closed stdout");
        ed_set_status_message("copilot: server exited");
        cp_proto_shutdown();
        return;
    }
    jrpc_reader_feed(&CP.reader, tmp, (size_t)n);

    char *body;
    size_t blen;
    while ((body = jrpc_reader_next(&CP.reader, &blen))) {
        cp_handle_message(body, (int)blen);
        free(body);
    }
}
