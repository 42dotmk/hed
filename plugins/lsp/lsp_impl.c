#include "hed.h"
#include "lsp.h"
#include "json_helpers.h"
#include "lsp_hooks.h"
#include "lsp_servers.h"
#include "select_loop.h"
#include "ui/abuf.h"
#include "lib/ansi.h"
#include "utils/bottom_ui.h"
#include "utils/quickfix.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>

void buf_row_insert_in(Buffer *buf, int at, const char *s, size_t len);

#define LSP_MAX_SERVERS   8
#define LSP_READ_BUF_SIZE 65536
#define LSP_PENDING_MAX   32

typedef enum {
    LSP_REQ_NONE = 0,
    LSP_REQ_HOVER,
    LSP_REQ_DEFINITION,
    LSP_REQ_COMPLETION,
} LspReqKind;

typedef struct {
    int        id;
    LspReqKind kind;
    /* For LSP_REQ_COMPLETION: the buffer + cursor position at request
     * time, so the response handler can compute the replacement range
     * even if the user kept typing while waiting. */
    int        buf_idx;
    int        req_line;
    int        req_col;
} LspPending;

struct LspServer {
    char *lang;
    char *root_uri;

    pid_t pid;   /* child PID if we spawned it; 0 if attached via :lsp_connect */

    int to_fd;   /* editor writes here  (server stdin)  */
    int from_fd; /* editor reads from here (server stdout) */

    int initialized;
    int next_id;

    /* Incoming message framing */
    char read_buf[LSP_READ_BUF_SIZE];
    int  read_buf_len;
    int  content_length; /* -1 = waiting for header */
    char *msg_body;
    int   msg_body_len;

    LspPending pending[LSP_PENDING_MAX];
};

static LspServer *g_servers[LSP_MAX_SERVERS];
static int        g_servers_count = 0;

/* Monotonically increasing document version counter.
 * LSP requires per-document versions to be strictly increasing;
 * a global counter that only ever grows satisfies that requirement. */
static int g_doc_version = 1;

/* ----- diagnostics store -------------------------------------------
 * Latest publishDiagnostics per-URI. Replaced wholesale every time the
 * server re-publishes for a file. Survives across :lsp_disconnect, so
 * the user can inspect the last known state. */

typedef struct {
    int   line;     /* 0-based, LSP coords */
    int   col;
    int   severity; /* 1=Error, 2=Warning, 3=Info, 4=Hint */
    char *message;
} LspDiag;

typedef struct {
    char    *uri;
    LspDiag *items; /* stb_ds */
} LspDiagFile;

static LspDiagFile *g_diags = NULL; /* stb_ds */

/* Pending completion request that hit a server still in `initialize`.
 * We replay it once the server is ready so the user doesn't have to
 * press Ctrl+Space twice on the first invocation. */
typedef struct {
    char *lang;     /* server we're waiting for; NULL = no pending request */
    int   buf_idx;
    int   line;
    int   col;
} LspDeferredCompletion;
static LspDeferredCompletion g_deferred_completion = {0};

static void lsp_clear_deferred_completion(void) {
    free(g_deferred_completion.lang);
    g_deferred_completion.lang = NULL;
}

/* ------------------------------------------------------------------ helpers */

static LspServer *lsp_server_for_lang(const char *lang) {
    if (!lang) return NULL;
    for (int i = 0; i < LSP_MAX_SERVERS; i++) {
        if (g_servers[i] && strcmp(g_servers[i]->lang, lang) == 0)
            return g_servers[i];
    }
    return NULL;
}

static LspServer *lsp_server_for_buffer(Buffer *buf) {
    if (!buf || !buf->filetype) return NULL;
    return lsp_server_for_lang(buf->filetype);
}

/* Build full document text from buffer rows (not from disk). */
static char *lsp_build_content(Buffer *buf) {
    size_t total = 0;
    for (int i = 0; i < buf->num_rows; i++)
        total += buf->rows[i].chars.len + 1; /* +1 for '\n' */
    char  *s   = malloc(total + 1);
    if (!s) return NULL;
    size_t off = 0;
    for (int i = 0; i < buf->num_rows; i++) {
        memcpy(s + off, buf->rows[i].chars.data, buf->rows[i].chars.len);
        off += buf->rows[i].chars.len;
        s[off++] = '\n';
    }
    s[off] = '\0';
    return s;
}

static char *lsp_get_file_uri(const char *filepath) {
    if (!filepath) return NULL;
    char  cwd[1024] = {0};
    char *uri        = malloc(strlen(filepath) + 32);
    if (!uri) return NULL;
    if (filepath[0] == '/') {
        sprintf(uri, "file://%s", filepath);
    } else {
        if (getcwd(cwd, sizeof(cwd)))
            sprintf(uri, "file://%s/%s", cwd, filepath);
        else
            sprintf(uri, "file://%s", filepath);
    }
    return uri;
}

/* Strip "file://" prefix from a URI to get a filesystem path. */
static const char *lsp_uri_to_path(const char *uri) {
    if (!uri) return NULL;
    if (strncmp(uri, "file://", 7) == 0) return uri + 7;
    return uri;
}

/* -------------------------------------------------- pending request table */

static void lsp_pending_add(LspServer *srv, int id, LspReqKind kind) {
    for (int i = 0; i < LSP_PENDING_MAX; i++) {
        if (srv->pending[i].kind == LSP_REQ_NONE) {
            srv->pending[i] = (LspPending){ .id = id, .kind = kind };
            return;
        }
    }
    log_msg("LSP: pending table full, dropping request id=%d", id);
}

static void lsp_pending_add_completion(LspServer *srv, int id,
                                       int buf_idx, int line, int col) {
    for (int i = 0; i < LSP_PENDING_MAX; i++) {
        if (srv->pending[i].kind == LSP_REQ_NONE) {
            srv->pending[i] = (LspPending){
                .id = id, .kind = LSP_REQ_COMPLETION,
                .buf_idx = buf_idx, .req_line = line, .req_col = col,
            };
            return;
        }
    }
    log_msg("LSP: pending table full, dropping request id=%d", id);
}

static LspPending lsp_pending_pop(LspServer *srv, int id) {
    LspPending empty = { .kind = LSP_REQ_NONE };
    for (int i = 0; i < LSP_PENDING_MAX; i++) {
        if (srv->pending[i].kind != LSP_REQ_NONE && srv->pending[i].id == id) {
            LspPending p = srv->pending[i];
            srv->pending[i] = empty;
            return p;
        }
    }
    return empty;
}

/* ------------------------------------------------------- send primitives */

static void lsp_send_raw(LspServer *srv, const char *json_str) {
    if (!srv || srv->to_fd < 0) return;
    char header[64];
    int  clen = (int)strlen(json_str);
    int  hlen = snprintf(header, sizeof(header), "Content-Length: %d\r\n\r\n", clen);
    write(srv->to_fd, header, hlen);
    write(srv->to_fd, json_str, clen);
}

static void lsp_send_request(LspServer *srv, const char *method,
                              cJSON *params, int req_id) {
    if (!srv || srv->to_fd < 0) return;
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(req, "id", req_id);
    cJSON_AddStringToObject(req, "method", method);
    if (params) cJSON_AddItemToObject(req, "params", params);
    char *s = cJSON_PrintUnformatted(req);
    if (s) { lsp_send_raw(srv, s); free(s); }
    cJSON_Delete(req);
    log_msg("LSP[%s]: → %s id=%d", srv->lang, method, req_id);
}

static void lsp_send_notification(LspServer *srv, const char *method,
                                   cJSON *params) {
    if (!srv || srv->to_fd < 0) return;
    cJSON *notif = cJSON_CreateObject();
    cJSON_AddStringToObject(notif, "jsonrpc", "2.0");
    cJSON_AddStringToObject(notif, "method", method);
    if (params) cJSON_AddItemToObject(notif, "params", params);
    char *s = cJSON_PrintUnformatted(notif);
    if (s) { lsp_send_raw(srv, s); free(s); }
    cJSON_Delete(notif);
    log_msg("LSP[%s]: → %s (notification)", srv->lang, method);
}

/* ------------------------------------------------ lifecycle / handshake */

static void lsp_send_initialize(LspServer *srv) {
    cJSON *params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "processId", (double)getpid());

    cJSON *info = cJSON_CreateObject();
    cJSON_AddStringToObject(info, "name", "hed");
    cJSON_AddStringToObject(info, "version", HED_VERSION);
    cJSON_AddItemToObject(params, "clientInfo", info);

    cJSON_AddStringToObject(params, "rootUri", srv->root_uri);

    /* Declare client capabilities */
    cJSON *caps   = cJSON_CreateObject();
    cJSON *tdoc   = cJSON_CreateObject();

    cJSON *sync   = cJSON_CreateObject();
    cJSON_AddBoolToObject(sync,  "dynamicRegistration", 0);
    cJSON_AddBoolToObject(sync,  "willSave",            0);
    cJSON_AddBoolToObject(sync,  "willSaveWaitUntil",   0);
    cJSON_AddBoolToObject(sync,  "didSave",             1);
    cJSON_AddItemToObject(tdoc,  "synchronization",     sync);

    cJSON *hover = cJSON_CreateObject();
    cJSON_AddBoolToObject(hover, "dynamicRegistration", 0);
    cJSON *hfmt  = cJSON_CreateArray();
    cJSON_AddItemToArray(hfmt, cJSON_CreateString("plaintext"));
    cJSON_AddItemToArray(hfmt, cJSON_CreateString("markdown"));
    cJSON_AddItemToObject(hover, "contentFormat", hfmt);
    cJSON_AddItemToObject(tdoc, "hover", hover);

    cJSON *def   = cJSON_CreateObject();
    cJSON_AddBoolToObject(def, "dynamicRegistration", 0);
    cJSON_AddItemToObject(tdoc, "definition", def);

    cJSON *comp  = cJSON_CreateObject();
    cJSON_AddBoolToObject(comp, "dynamicRegistration", 0);
    cJSON_AddItemToObject(tdoc, "completion", comp);

    cJSON_AddItemToObject(caps, "textDocument", tdoc);
    cJSON_AddItemToObject(params, "capabilities", caps);

    int id = srv->next_id++;
    lsp_pending_add(srv, id, LSP_REQ_NONE); /* initialize has no user action */
    lsp_send_request(srv, "initialize", params, id);
}

static void lsp_send_initialized(LspServer *srv) {
    lsp_send_notification(srv, "initialized", cJSON_CreateObject());
    srv->initialized = 1;
    log_msg("LSP[%s]: handshake complete", srv->lang);
    ed_set_status_message("LSP[%s]: connected", srv->lang);
}

static void lsp_notify_existing_buffers(LspServer *srv) {
    for (ptrdiff_t i = 0; i < arrlen(E.buffers); i++) {
        Buffer *buf = &E.buffers[i];
        if (buf && buf->filetype && buf->filename &&
            strcmp(buf->filetype, srv->lang) == 0)
            lsp_on_buffer_open(buf);
    }
}

/* ------------------------------------------ response / notification handlers */

/* Strip basic markdown inline markers in-place: **x** *x* `x` -> x */
static void strip_markdown_inline(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if (r[0] == '*' && r[1] == '*') { r += 2; continue; }
        if (r[0] == '*')               { r += 1; continue; }
        if (r[0] == '`')               { r += 1; continue; }
        *w++ = *r++;
    }
    *w = '\0';
}

/* Create a read-only popup modal from multi-line text and show it.
 * Dismiss with q or <Esc> (handled in ed_process_keypress). */
static void lsp_show_popup(const char *title, const char *text) {
    if (!text || !*text) {
        ed_set_status_message("LSP: (empty response)");
        return;
    }
    log_msg("LSP popup [%s]: %s", title, text);

    /* First pass: split lines, skip bare file paths, measure dimensions.
     * A "bare path" is a line whose first non-space char is '/' and contains
     * no spaces — clangd appends these after the actual hover content. */
    int   max_width  = 0;
    int   nlines     = 0;
    char  src_label[256] = {0}; /* basename of the last skipped path line */
    const char *p    = text;
    while (*p) {
        int len = 0;
        while (p[len] && p[len] != '\n') len++;
        int w = len;
        while (w > 0 && (p[w-1] == '\r' || p[w-1] == ' ')) w--;
        /* detect bare path: starts with '/', no spaces */
        int is_path = (w > 0 && p[0] == '/');
        if (is_path) {
            for (int i = 1; i < w; i++) {
                if (p[i] == ' ') { is_path = 0; break; }
            }
        }
        if (is_path) {
            /* extract basename for bottom label */
            const char *slash = p;
            for (int i = 0; i < w; i++)
                if (p[i] == '/') slash = p + i + 1;
            int blen = (int)(p + w - slash);
            if (blen > (int)sizeof(src_label) - 1) blen = (int)sizeof(src_label) - 1;
            memcpy(src_label, slash, (size_t)blen);
            src_label[blen] = '\0';
        } else {
            if (w > max_width) max_width = w;
            nlines++;
        }
        p += len + (p[len] == '\n' ? 1 : 0);
    }
    /* strip trailing blank content lines */
    if (nlines == 0) { ed_set_status_message("LSP: (empty)"); return; }

    /* Cap dimensions to screen, leave room for border (drawn outside rect) */
    int width  = max_width;
    int height = nlines;
    if (width  < 20)                width  = 20;
    if (width  > E.screen_cols - 6) width  = E.screen_cols - 6;
    if (height > E.screen_rows - 6) height = E.screen_rows - 6;

    /* Create scratch buffer — no filename (no top title), title = src label */
    int     buf_idx = -1;
    if (buf_new(NULL, &buf_idx) != ED_OK) return;
    Buffer *buf = &E.buffers[buf_idx];
    free(buf->filename); buf->filename = NULL;
    free(buf->title);
    buf->title = src_label[0] ? strdup(src_label) : NULL;

    /* Second pass: insert non-path lines into the buffer */
    p = text;
    int row = 0;
    while (*p) {
        int len = 0;
        while (p[len] && p[len] != '\n') len++;
        char *line = malloc((size_t)len + 1);
        if (!line) break;
        memcpy(line, p, (size_t)len);
        line[len] = '\0';
        /* strip \r */
        int w = len;
        if (w > 0 && line[w-1] == '\r') line[--w] = '\0';
        /* skip bare path lines */
        int is_path = (w > 0 && line[0] == '/');
        if (is_path) {
            for (int i = 1; i < w; i++) {
                if (line[i] == ' ') { is_path = 0; break; }
            }
        }
        if (!is_path) {
            strip_markdown_inline(line);
            buf_row_insert_in(buf, row, line, (size_t)strlen(line));
            row++;
        }
        free(line);
        p += len + (p[len] == '\n' ? 1 : 0);
    }
    buf->dirty    = 0;   /* reset so buf_close won't block */
    buf->readonly = 1;

    /* Create and show centered modal */
    Window *modal = winmodal_create(-1, -1, width, height);
    if (!modal) { buf->dirty = 0; buf_close(buf_idx); return; }
    modal->buffer_index = buf_idx;
    winmodal_show(modal);
    lsp_popup_track(modal);
    ed_set_status_message("q/<Esc> close  j/k scroll");
}

/* ----- completion ------------------------------------------------- */

/* ===== completion bottom-pane ====================================
 *
 * The completion UI is a transient overlay strip drawn at the bottom
 * of the screen via HOOK_RENDER_OVERLAY. It does not take focus —
 * typing continues in the source window — and it stays open across
 * keystrokes, re-issuing textDocument/completion on every char insert
 * so results follow what you type.
 *
 *   <C-Space>      toggle pane (insert mode)
 *   <C-n> / <Down> next item
 *   <C-p> / <Up>   previous item
 *   <Tab>          accept selected; insert at word-start; re-request
 *   <Esc>          close pane (does NOT exit insert mode)
 *
 * When the pane is closed nothing is sent to the LSP server. */

typedef struct {
    int    open;
    int    buf_idx;       /* host buffer for this session */
    int    anchor_line;   /* 0-based row in the buffer */
    int    word_start;    /* 0-based byte col where replacement begins */
    int    selected;      /* highlighted item index */
    int    scroll;        /* first visible item */
    int    height;        /* visible rows for items (capped) */
    int    n;
    char **labels;
    char **inserts;
} LspCompletionPane;

static LspCompletionPane g_pane = { 0 };

#define LSP_PANE_HEIGHT       8
#define LSP_PANE_MAX_ITEMS  200

static void lsp_pane_free_items(void) {
    for (int i = 0; i < g_pane.n; i++) {
        free(g_pane.labels[i]);
        free(g_pane.inserts[i]);
    }
    free(g_pane.labels);
    free(g_pane.inserts);
    g_pane.labels  = NULL;
    g_pane.inserts = NULL;
    g_pane.n       = 0;
}

static void lsp_pane_close_internal(void) {
    lsp_pane_free_items();
    memset(&g_pane, 0, sizeof(g_pane));
    g_pane.buf_idx = -1;
}

int lsp_completion_pane_is_open(void) { return g_pane.open; }

void lsp_completion_pane_close(void) {
    if (!g_pane.open) return;
    lsp_pane_close_internal();
}

/* Walk back over identifier characters to find the start of the
 * partial word being completed (0-based byte index). */
static int lsp_word_start_col(Buffer *buf, int line, int cx) {
    if (!buf || line < 0 || line >= buf->num_rows) return cx;
    const char *s = buf->rows[line].chars.data;
    int i = cx;
    while (i > 0) {
        unsigned char c = (unsigned char)s[i - 1];
        if (!(isalnum(c) || c == '_')) break;
        i--;
    }
    return i;
}

void lsp_completion_pane_toggle(void) {
    if (g_pane.open) { lsp_pane_close_internal(); return; }
    Buffer *buf = buf_cur();
    if (!buf || !buf->cursor || !buf->filename) {
        ed_set_status_message("LSP completion: no buffer");
        return;
    }
    int buf_idx = (int)(buf - E.buffers);
    g_pane.open        = 1;
    g_pane.buf_idx     = buf_idx;
    g_pane.anchor_line = buf->cursor->y;
    g_pane.word_start  = lsp_word_start_col(buf, buf->cursor->y, buf->cursor->x);
    g_pane.selected    = 0;
    g_pane.scroll      = 0;
    g_pane.height      = LSP_PANE_HEIGHT;
    /* Kick off the first completion request. The response handler
     * will populate items; the renderer shows "loading…" until then. */
    lsp_request_completion(buf, buf->cursor->y, buf->cursor->x);
}

/* Apply the highlighted item: replace [word_start, cursor) with the
 * insertText, move cursor to end, then re-request to refresh. */
static void lsp_pane_accept(void) {
    if (!g_pane.open || g_pane.n <= 0) return;
    int idx = g_pane.selected;
    if (idx < 0 || idx >= g_pane.n) return;
    if (g_pane.buf_idx < 0 || g_pane.buf_idx >= (int)arrlen(E.buffers)) return;

    Buffer *buf  = &E.buffers[g_pane.buf_idx];
    int     line = g_pane.anchor_line;
    if (line < 0 || line >= buf->num_rows) return;

    Window *hw = NULL;
    for (ptrdiff_t w = 0; w < arrlen(E.windows); w++) {
        if (E.windows[w].buffer_index == g_pane.buf_idx) {
            hw = &E.windows[w]; break;
        }
    }
    int word_cx = g_pane.word_start;
    int cur_cx  = hw ? hw->cursor.x : (buf->cursor ? buf->cursor->x : word_cx);
    if (cur_cx < word_cx) cur_cx = word_cx;
    Row *row = &buf->rows[line];
    if (cur_cx > (int)row->chars.len) cur_cx = (int)row->chars.len;

    const char *ins  = g_pane.inserts[idx];
    size_t      ilen = strlen(ins);
    size_t      tail = row->chars.len - (size_t)cur_cx;
    SizedStr    fresh = sstr_new();
    sstr_reserve(&fresh, (size_t)word_cx + ilen + tail);
    sstr_append(&fresh, row->chars.data, (size_t)word_cx);
    sstr_append(&fresh, ins, ilen);
    sstr_append(&fresh, row->chars.data + cur_cx, tail);
    sstr_free(&row->chars);
    row->chars = fresh;
    buf_row_update(row);

    int new_cx = word_cx + (int)ilen;
    if (buf->cursor) { buf->cursor->x = new_cx; buf->cursor->y = line; }
    if (hw)          { hw->cursor.x  = new_cx; hw->cursor.y  = line; }
    buf->dirty++;

    /* Re-anchor at the new cursor position and re-request to refresh
     * (e.g., after picking `foo` we may want fields/methods of foo). */
    g_pane.anchor_line = line;
    g_pane.word_start  = lsp_word_start_col(buf, line, new_cx);
    g_pane.selected    = 0;
    g_pane.scroll      = 0;
    /* Drop current items so the renderer shows "loading…" until the
     * next response lands. */
    lsp_pane_free_items();
    lsp_request_completion(buf, line, new_cx);
}

static void lsp_pane_move(int delta) {
    if (!g_pane.open || g_pane.n <= 0) return;
    g_pane.selected += delta;
    if (g_pane.selected < 0)            g_pane.selected = 0;
    if (g_pane.selected >= g_pane.n)    g_pane.selected = g_pane.n - 1;
    /* Keep selection in view */
    if (g_pane.selected < g_pane.scroll)
        g_pane.scroll = g_pane.selected;
    int last_visible = g_pane.scroll + g_pane.height - 1;
    if (g_pane.selected > last_visible)
        g_pane.scroll = g_pane.selected - g_pane.height + 1;
    if (g_pane.scroll < 0) g_pane.scroll = 0;
}

/* Build labels + inserts from a CompletionList JSON. */
static int lsp_compl_extract(cJSON *result,
                             char ***out_labels, char ***out_inserts) {
    *out_labels = NULL; *out_inserts = NULL;
    cJSON *items = cJSON_IsArray(result) ? result
                                         : json_get_array(result, "items");
    int n = items ? cJSON_GetArraySize(items) : 0;
    if (n <= 0) return 0;
    if (n > LSP_PANE_MAX_ITEMS) n = LSP_PANE_MAX_ITEMS;

    char **labels  = malloc(sizeof(char *) * (size_t)n);
    char **inserts = malloc(sizeof(char *) * (size_t)n);
    if (!labels || !inserts) { free(labels); free(inserts); return 0; }

    int kept = 0;
    for (int i = 0; i < n; i++) {
        cJSON      *it     = cJSON_GetArrayItem(items, i);
        const char *label  = json_get_string(it, "label");
        const char *insert = json_get_string(it, "insertText");
        const char *detail = json_get_string(it, "detail");
        if (!insert || !*insert) insert = label;
        if (!label  || !*label)  continue;
        char tmp[512];
        if (detail && *detail) snprintf(tmp, sizeof(tmp), "%s : %s", label, detail);
        else                   snprintf(tmp, sizeof(tmp), "%s", label);
        labels[kept]  = strdup(tmp);
        inserts[kept] = strdup(insert);
        kept++;
    }
    if (kept == 0) { free(labels); free(inserts); return 0; }
    *out_labels  = labels;
    *out_inserts = inserts;
    return kept;
}

static void lsp_handle_completion_result(LspServer *srv,
                                         const LspPending *pop,
                                         cJSON *result) {
    /* If the user closed the pane while the request was in flight,
     * silently drop the response. */
    if (!g_pane.open) return;
    /* Only accept responses that match the open session's buffer. */
    if (pop->buf_idx != g_pane.buf_idx) return;

    char **labels  = NULL;
    char **inserts = NULL;
    int    kept    = 0;
    if (result && !cJSON_IsNull(result))
        kept = lsp_compl_extract(result, &labels, &inserts);

    /* Replace the items either way (zero items also clears stale data). */
    lsp_pane_free_items();
    g_pane.labels   = labels;
    g_pane.inserts  = inserts;
    g_pane.n        = kept;
    g_pane.selected = 0;
    g_pane.scroll   = 0;
    if (kept == 0) {
        log_msg("LSP[%s]: no completions", srv->lang);
    }
}

/* ---- pane render & input ---- */

static void lsp_pane_draw(struct Abuf *ab) {
    if (!g_pane.open) return;
    Layout lo;
    layout_compute(&lo);
    /* Place pane just above the status bar, eating the bottom of the
     * content area. status_row is 1-based; pane_top is the first row
     * we'll draw on. */
    int pane_top = lo.status_row - g_pane.height;
    if (pane_top < 1) {
        /* Screen too small; cap to whatever rows we have. */
        g_pane.height = lo.status_row - 1;
        pane_top      = 1;
        if (g_pane.height < 1) return;
    }

    int cols = lo.term_cols > 0 ? lo.term_cols : 80;
    if (cols > 256) cols = 256;
    char line[260];

    for (int r = 0; r < g_pane.height; r++) {
        int item_idx = g_pane.scroll + r;
        ansi_move(ab, pane_top + r, 1);
        ansi_clear_eol(ab);

        if (item_idx == 0 && g_pane.n == 0) {
            const char *empty = "  (no completions — type or press <C-Space> to close)";
            int len = (int)strlen(empty);
            if (len > cols) len = cols;
            ab_append(ab, empty, len);
            continue;
        }
        if (item_idx >= g_pane.n) continue;

        int is_sel = (item_idx == g_pane.selected);
        if (is_sel) ab_append(ab, "\x1b[7m", 4); /* reverse video */

        const char *label = g_pane.labels[item_idx];
        int n = snprintf(line, sizeof(line), "  %s", label);
        if (n < 0) n = 0;
        if (n > cols) n = cols;
        ab_append(ab, line, n);
        /* pad to full width so the highlight bar is visible */
        for (int p = n; p < cols; p++) ab_append(ab, " ", 1);

        if (is_sel) ab_append(ab, "\x1b[m", 3);
    }

    /* Top divider */
    if (pane_top >= 2) {
        ansi_move(ab, pane_top - 1, 1);
        ansi_clear_eol(ab);
        char hdr[80];
        int  hl = snprintf(hdr, sizeof(hdr),
                           "── completion %d/%d ", g_pane.selected + 1,
                           g_pane.n > 0 ? g_pane.n : 0);
        if (hl < 0) hl = 0;
        if (hl > cols) hl = cols;
        ab_append(ab, hdr, hl);
        for (int p = hl; p < cols; p++) ab_append(ab, "─", 3);
    }
}

/* Keypress hook: while the pane is open, intercept selection/accept/
 * close keys. Everything else (printable chars, backspace, etc.)
 * falls through to normal insert-mode handling — and HOOK_CHAR_INSERT
 * (registered separately) will re-issue the completion request. */
static void lsp_pane_keypress(HookKeyEvent *event) {
    if (!event || !g_pane.open) return;
    int key = event->key;

    if (key == KEY_ARROW_DOWN || key == ('n' & 0x1f)) {
        lsp_pane_move(+1); event->consumed = 1; return;
    }
    if (key == KEY_ARROW_UP   || key == ('p' & 0x1f)) {
        lsp_pane_move(-1); event->consumed = 1; return;
    }
    if (key == '\t') {
        lsp_pane_accept(); event->consumed = 1; return;
    }
    if (key == '\x1b') {
        /* Esc closes the pane; the user can press Esc again to leave
         * INSERT mode. We consume so the editor doesn't switch modes
         * unexpectedly. */
        lsp_pane_close_internal();
        event->consumed = 1;
        return;
    }
    /* Treat C-Space (NUL byte) as the toggle even while open → close. */
    if (key == 0) {
        lsp_pane_close_internal();
        event->consumed = 1;
        return;
    }
}

/* HOOK_CHAR_INSERT: any printable typed in INSERT mode fires this
 * after the editor has applied the edit. While the pane is open we
 * use it to re-issue the completion request. We also close the pane
 * if the user moved off the anchor line or backspaced past the word
 * start (HOOK_CHAR_INSERT only fires for inserts; for backspace we
 * use HOOK_LINE_DELETE — handled in lsp_pane_line_delete below). */
static void lsp_pane_char_insert(const HookCharEvent *ev) {
    if (!g_pane.open || !ev || !ev->buf) return;
    int buf_idx = (int)(ev->buf - E.buffers);
    if (buf_idx != g_pane.buf_idx) return;
    if (ev->row != g_pane.anchor_line) {
        lsp_pane_close_internal(); return;
    }
    /* Re-request completion at the new cursor position. */
    lsp_request_completion(ev->buf, ev->row, ev->col);
}

static void lsp_pane_cursor_move(const HookCursorEvent *ev) {
    if (!g_pane.open || !ev || !ev->buf) return;
    int buf_idx = (int)(ev->buf - E.buffers);
    if (buf_idx != g_pane.buf_idx) {
        lsp_pane_close_internal(); return;
    }
    if (ev->new_y != g_pane.anchor_line ||
        ev->new_x < g_pane.word_start) {
        lsp_pane_close_internal(); return;
    }
    /* Cursor moved horizontally on the same line (e.g., backspace).
     * Re-request so the list narrows/widens with the new prefix. */
    if (ev->new_x != ev->old_x) {
        lsp_request_completion(ev->buf, ev->new_y, ev->new_x);
    }
}

static void lsp_handle_hover_result(cJSON *result) {
    if (!result || cJSON_IsNull(result)) {
        ed_set_status_message("LSP hover: no info");
        return;
    }

    /* contents: string | { kind, value } | MarkedString[] */
    cJSON      *contents = cJSON_GetObjectItemCaseSensitive(result, "contents");
    const char *text     = NULL;

    if (cJSON_IsString(contents)) {
        text = contents->valuestring;
    } else if (cJSON_IsObject(contents)) {
        cJSON *val = cJSON_GetObjectItemCaseSensitive(contents, "value");
        if (val && cJSON_IsString(val)) text = val->valuestring;
    } else if (cJSON_IsArray(contents)) {
        /* concatenate all entries */
        static char combined[4096];
        combined[0] = '\0';
        int off = 0;
        for (int i = 0; i < cJSON_GetArraySize(contents) && off < (int)sizeof(combined) - 2; i++) {
            cJSON *item = cJSON_GetArrayItem(contents, i);
            const char *v = NULL;
            if (cJSON_IsString(item)) v = item->valuestring;
            else {
                cJSON *val = cJSON_GetObjectItemCaseSensitive(item, "value");
                if (val && cJSON_IsString(val)) v = val->valuestring;
            }
            if (v && *v) {
                if (off > 0) combined[off++] = '\n';
                int rem = (int)sizeof(combined) - off - 1;
                int len = (int)strlen(v);
                if (len > rem) len = rem;
                memcpy(combined + off, v, (size_t)len);
                off += len;
                combined[off] = '\0';
            }
        }
        if (combined[0]) text = combined;
    }

    if (!text || !*text) {
        ed_set_status_message("LSP hover: (empty)");
        return;
    }

    lsp_show_popup("Hover", text);
}

static void lsp_handle_definition_result(cJSON *result) {
    if (!result || cJSON_IsNull(result)) {
        ed_set_status_message("LSP: definition not found");
        return;
    }

    /* result can be Location | Location[] | LocationLink[] */
    cJSON *loc = cJSON_IsArray(result) ? cJSON_GetArrayItem(result, 0) : result;
    if (!loc) {
        ed_set_status_message("LSP: definition not found");
        return;
    }

    const char *uri = json_get_string(loc, "uri");
    if (!uri) uri = json_get_string(loc, "targetUri"); /* LocationLink */
    if (!uri) {
        ed_set_status_message("LSP: definition missing uri");
        return;
    }

    cJSON *range = json_get_object(loc, "range");
    if (!range) range = json_get_object(loc, "targetSelectionRange");
    int line = 0, col = 0;
    if (range) {
        cJSON *start = json_get_object(range, "start");
        if (start) {
            line = json_get_int(start, "line",      0);
            col  = json_get_int(start, "character", 0);
        }
    }

    const char *path = lsp_uri_to_path(uri);
    log_msg("LSP definition: %s:%d:%d", path, line + 1, col + 1);

    buf_open_or_switch(path, true);
    Buffer *buf = buf_cur();
    if (buf) {
        buf->cursor->y = line < buf->num_rows ? line : buf->num_rows - 1;
        buf->cursor->x = col;
    }
    ed_set_status_message("LSP: jumped to %s:%d", path, line + 1);
}

static void lsp_process_response(LspServer *srv, cJSON *json) {
    cJSON *id_node = cJSON_GetObjectItemCaseSensitive(json, "id");
    int    id      = id_node ? (int)id_node->valuedouble : -1;

    cJSON *error = cJSON_GetObjectItemCaseSensitive(json, "error");
    if (error) {
        const char *msg = json_get_string(error, "message");
        log_msg("LSP[%s]: error id=%d: %s", srv->lang, id, msg ? msg : "?");
        ed_set_status_message("LSP error: %s", msg ? msg : "unknown");
        lsp_pending_pop(srv, id);
        return;
    }

    cJSON *result = cJSON_GetObjectItemCaseSensitive(json, "result");

    /* initialize response */
    if (!srv->initialized) {
        lsp_pending_pop(srv, id);
        lsp_send_initialized(srv);
        lsp_notify_existing_buffers(srv);
        /* Replay a completion request that arrived during the handshake,
         * so the user doesn't have to press C-Space again once init
         * completes. didOpen has just been sent so the server has the
         * doc state it needs. */
        if (g_deferred_completion.lang &&
            strcmp(g_deferred_completion.lang, srv->lang) == 0 &&
            g_deferred_completion.buf_idx >= 0 &&
            g_deferred_completion.buf_idx < (int)arrlen(E.buffers)) {
            Buffer *dbuf = &E.buffers[g_deferred_completion.buf_idx];
            int    dline = g_deferred_completion.line;
            int    dcol  = g_deferred_completion.col;
            lsp_clear_deferred_completion();
            lsp_request_completion(dbuf, dline, dcol);
        } else {
            lsp_clear_deferred_completion();
        }
        return;
    }

    LspPending pop  = lsp_pending_pop(srv, id);
    LspReqKind kind = pop.kind;
    switch (kind) {
    case LSP_REQ_HOVER:
        lsp_handle_hover_result(result);
        break;
    case LSP_REQ_DEFINITION:
        lsp_handle_definition_result(result);
        break;
    case LSP_REQ_COMPLETION:
        lsp_handle_completion_result(srv, &pop, result);
        break;
    default:
        log_msg("LSP[%s]: untracked response id=%d", srv->lang, id);
        break;
    }
}

/* Find or create the diagnostics slot for a URI. Returns a pointer
 * into the g_diags array; valid until the next push/erase. */
static LspDiagFile *lsp_diag_slot(const char *uri) {
    for (ptrdiff_t i = 0; i < arrlen(g_diags); i++) {
        if (strcmp(g_diags[i].uri, uri) == 0) return &g_diags[i];
    }
    LspDiagFile slot = { .uri = strdup(uri), .items = NULL };
    arrput(g_diags, slot);
    return &g_diags[arrlen(g_diags) - 1];
}

static void lsp_diag_clear_slot(LspDiagFile *slot) {
    for (ptrdiff_t i = 0; i < arrlen(slot->items); i++)
        free(slot->items[i].message);
    arrsetlen(slot->items, 0);
}

/* Replace the diagnostics for `uri` with the LSP `diag` array. */
static void lsp_diag_replace(const char *uri, cJSON *diag, int n) {
    LspDiagFile *slot = lsp_diag_slot(uri);
    lsp_diag_clear_slot(slot);
    for (int i = 0; i < n; i++) {
        cJSON *d     = cJSON_GetArrayItem(diag, i);
        cJSON *range = json_get_object(d, "range");
        cJSON *start = range ? json_get_object(range, "start") : NULL;
        int    line  = start ? json_get_int(start, "line",      0) : 0;
        int    col   = start ? json_get_int(start, "character", 0) : 0;
        int    sev   = json_get_int(d, "severity", 1);
        const char *msg = json_get_string(d, "message");
        LspDiag e = {
            .line = line, .col = col, .severity = sev,
            .message = strdup(msg ? msg : ""),
        };
        arrput(slot->items, e);
    }
}

/* Dump every stored diagnostic into the global quickfix list and open it. */
void lsp_cmd_diagnostics(void) {
    qf_clear(&E.qf);
    int total = 0;
    for (ptrdiff_t f = 0; f < arrlen(g_diags); f++) {
        LspDiagFile *df = &g_diags[f];
        const char  *fp = lsp_uri_to_path(df->uri);
        for (ptrdiff_t i = 0; i < arrlen(df->items); i++) {
            LspDiag *d = &df->items[i];
            const char *sev = (d->severity == 1) ? "E"
                            : (d->severity == 2) ? "W"
                            : (d->severity == 3) ? "I" : "H";
            char text[1024];
            snprintf(text, sizeof(text), "[%s] %s", sev, d->message);
            qf_add(&E.qf, fp, d->line + 1, d->col + 1, text);
            total++;
        }
    }
    if (total == 0) {
        ed_set_status_message("LSP: no diagnostics");
        return;
    }
    qf_open(&E.qf, E.qf.height > 0 ? E.qf.height : 8);
    ed_set_status_message("LSP: %d diagnostic(s)", total);
}

static void lsp_process_notification(LspServer *srv, cJSON *json) {
    const char *method = json_get_string(json, "method");
    if (!method) return;
    log_msg("LSP[%s]: ← %s", srv->lang, method);

    if (strcmp(method, "textDocument/publishDiagnostics") == 0) {
        cJSON *params = json_get_object(json, "params");
        if (!params) return;
        const char *uri  = json_get_string(params, "uri");
        cJSON      *diag = json_get_array(params, "diagnostics");
        int         cnt  = diag ? cJSON_GetArraySize(diag) : 0;
        log_msg("LSP[%s]: diagnostics for %s: %d items", srv->lang,
                uri ? uri : "?", cnt);
        if (uri) lsp_diag_replace(uri, diag, cnt);
    } else if (strcmp(method, "window/showMessage") == 0) {
        cJSON      *params = json_get_object(json, "params");
        const char *msg    = params ? json_get_string(params, "message") : NULL;
        if (msg) ed_set_status_message("LSP: %s", msg);
    } else if (strcmp(method, "window/logMessage") == 0) {
        cJSON      *params = json_get_object(json, "params");
        const char *msg    = params ? json_get_string(params, "message") : NULL;
        if (msg) log_msg("LSP[%s] server log: %s", srv->lang, msg);
    }
}

static void lsp_handle_message(LspServer *srv, const char *msg, int len) {
    log_msg("LSP[%s]: message len=%d: %.120s", srv->lang, len, msg);
    cJSON *json = json_parse(msg, (size_t)len);
    if (!json) { log_msg("LSP: JSON parse error"); return; }

    cJSON *id = cJSON_GetObjectItemCaseSensitive(json, "id");
    if (id && !cJSON_IsNull(id))
        lsp_process_response(srv, json);
    else
        lsp_process_notification(srv, json);

    cJSON_Delete(json);
}

/* -------------------------------------------------------- public API: lifecycle */

void lsp_init(void) {
    for (int i = 0; i < LSP_MAX_SERVERS; i++) g_servers[i] = NULL;
    g_servers_count = 0;
    g_pane.buf_idx  = -1;

    /* Completion-pane hooks: render overlay, key interception while
     * the pane is open, and reactivity to typing/cursor moves. */
    hook_register_render_overlay(lsp_pane_draw);
    hook_register_key(HOOK_KEYPRESS, lsp_pane_keypress);
    hook_register_char(HOOK_CHAR_INSERT, MODE_INSERT, "*", lsp_pane_char_insert);
    hook_register_cursor(HOOK_CURSOR_MOVE, -1, "*", lsp_pane_cursor_move);

    log_msg("LSP: init");
}

static void lsp_on_readable(int fd, void *ud);

static void lsp_reap_child(LspServer *srv) {
    if (srv->pid <= 0) return;
    /* Closing stdin should make most LSPs exit; give them a moment, then
     * SIGTERM, then reap. WNOHANG keeps us non-blocking. */
    for (int i = 0; i < 5; i++) {
        int status = 0;
        pid_t r = waitpid(srv->pid, &status, WNOHANG);
        if (r == srv->pid || r < 0) { srv->pid = 0; return; }
        if (i == 1) kill(srv->pid, SIGTERM);
        if (i == 3) kill(srv->pid, SIGKILL);
        struct timespec ts = { 0, 20 * 1000 * 1000 }; /* 20ms */
        nanosleep(&ts, NULL);
    }
    srv->pid = 0;
}

static void lsp_close_fds(LspServer *srv) {
    if (srv->from_fd >= 0) ed_loop_unregister(srv->from_fd);
    if (srv->to_fd >= 0) close(srv->to_fd);
    if (srv->from_fd >= 0 && srv->from_fd != srv->to_fd) close(srv->from_fd);
    srv->to_fd = srv->from_fd = -1;
    lsp_reap_child(srv);
}

void lsp_shutdown(void) {
    for (int i = 0; i < LSP_MAX_SERVERS; i++) {
        if (!g_servers[i]) continue;
        LspServer *srv = g_servers[i];
        lsp_close_fds(srv);
        free(srv->lang);
        free(srv->root_uri);
        free(srv->msg_body);
        free(srv);
        g_servers[i] = NULL;
    }
    g_servers_count = 0;
}

static void lsp_on_readable(int fd, void *ud) {
    LspServer *srv = ud;
    if (!srv || srv->from_fd != fd) return;

    int space = LSP_READ_BUF_SIZE - srv->read_buf_len;
    if (space <= 0) { srv->read_buf_len = 0; return; }

    ssize_t n = read(srv->from_fd, srv->read_buf + srv->read_buf_len, (size_t)space);
    if (n <= 0) {
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        log_msg("LSP[%s]: disconnected", srv->lang);
        char lang_copy[64];
        snprintf(lang_copy, sizeof(lang_copy), "%s", srv->lang);
        int   was_spawned = srv->pid > 0;
        lsp_close_fds(srv);
        srv->initialized = 0;
        if (was_spawned) {
            /* We owned the child; drop the record so auto-start can retry
             * on the next buffer open. Manually-attached servers (pid==0)
             * stay registered so the user can reconnect to the same one. */
            for (int i = 0; i < LSP_MAX_SERVERS; i++) {
                if (g_servers[i] == srv) {
                    g_servers[i] = NULL; g_servers_count--; break;
                }
            }
            free(srv->lang); free(srv->root_uri); free(srv->msg_body); free(srv);
        }
        ed_set_status_message("LSP[%s]: disconnected", lang_copy);
        return;
    }
    srv->read_buf_len += (int)n;

    /* Parse and dispatch complete messages */
    while (srv->read_buf_len > 0) {
        if (srv->content_length < 0) {
            char *hend = strstr(srv->read_buf, "\r\n\r\n");
            if (!hend) break;
            char *cl = strstr(srv->read_buf, "Content-Length:");
            if (!cl || cl > hend) { srv->read_buf_len = 0; break; }
            srv->content_length = atoi(cl + 15);
            int hlen = (int)(hend - srv->read_buf) + 4;
            memmove(srv->read_buf, hend + 4, (size_t)(srv->read_buf_len - hlen));
            srv->read_buf_len -= hlen;
            free(srv->msg_body);
            srv->msg_body     = malloc((size_t)srv->content_length + 1);
            srv->msg_body_len = 0;
        }

        if (srv->content_length >= 0 && srv->msg_body) {
            int need   = srv->content_length - srv->msg_body_len;
            int avail  = srv->read_buf_len;
            int copy   = need < avail ? need : avail;
            memcpy(srv->msg_body + srv->msg_body_len, srv->read_buf, (size_t)copy);
            srv->msg_body_len += copy;
            memmove(srv->read_buf, srv->read_buf + copy,
                    (size_t)(srv->read_buf_len - copy));
            srv->read_buf_len -= copy;

            if (srv->msg_body_len >= srv->content_length) {
                srv->msg_body[srv->content_length] = '\0';
                lsp_handle_message(srv, srv->msg_body, srv->content_length);
                free(srv->msg_body);
                srv->msg_body     = NULL;
                srv->msg_body_len = 0;
                srv->content_length = -1;
            }
        }

        if (srv->content_length < 0 && srv->read_buf_len == 0) break;
        /* If we didn't make progress, stop to avoid infinite loop */
        if (srv->content_length >= 0 && srv->read_buf_len == 0) break;
    }
}

/* ---------------------------------------------------- buffer notifications */

void lsp_on_buffer_open(Buffer *buf) {
    if (!buf || !buf->filename || !buf->filetype) return;
    LspServer *srv = lsp_server_for_buffer(buf);

    /* Auto-start: if no server is running for this filetype but the
     * registry has an entry, spawn it. The initialize-response handler
     * will replay didOpen for this buffer once the handshake completes,
     * so we just return here. */
    if (!srv && lsp_servers_lookup(buf->filetype)) {
        log_msg("LSP: auto-start triggered for %s (file=%s)",
                buf->filetype, buf->filename);
        lsp_cmd_start(buf->filetype, buf->filename);
        return;
    }
    if (!srv || !srv->initialized) return;

    char *uri     = lsp_get_file_uri(buf->filename);
    char *content = lsp_build_content(buf);
    if (!uri || !content) { free(uri); free(content); return; }

    cJSON *params   = cJSON_CreateObject();
    cJSON *textdoc  = cJSON_CreateObject();
    cJSON_AddStringToObject(textdoc, "uri",        uri);
    cJSON_AddStringToObject(textdoc, "languageId", buf->filetype);
    cJSON_AddNumberToObject(textdoc, "version",    g_doc_version++);
    cJSON_AddStringToObject(textdoc, "text",       content);
    cJSON_AddItemToObject(params, "textDocument", textdoc);

    lsp_send_notification(srv, "textDocument/didOpen", params);
    free(uri); free(content);
}

void lsp_on_buffer_close(Buffer *buf) {
    if (!buf || !buf->filename) return;
    LspServer *srv = lsp_server_for_buffer(buf);
    if (!srv || !srv->initialized) return;

    char *uri = lsp_get_file_uri(buf->filename);
    if (!uri) return;

    cJSON *params  = cJSON_CreateObject();
    cJSON *textdoc = cJSON_CreateObject();
    cJSON_AddStringToObject(textdoc, "uri", uri);
    cJSON_AddItemToObject(params, "textDocument", textdoc);
    lsp_send_notification(srv, "textDocument/didClose", params);
    free(uri);
}

void lsp_on_buffer_save(Buffer *buf) {
    if (!buf || !buf->filename) return;
    LspServer *srv = lsp_server_for_buffer(buf);
    if (!srv || !srv->initialized) return;

    char *uri = lsp_get_file_uri(buf->filename);
    if (!uri) return;

    cJSON *params  = cJSON_CreateObject();
    cJSON *textdoc = cJSON_CreateObject();
    cJSON_AddStringToObject(textdoc, "uri", uri);
    cJSON_AddItemToObject(params, "textDocument", textdoc);
    lsp_send_notification(srv, "textDocument/didSave", params);
    free(uri);
}

/* Full-document sync — called when leaving INSERT mode. */
void lsp_on_buffer_changed(Buffer *buf) {
    if (!buf || !buf->filename || !buf->filetype) return;
    LspServer *srv = lsp_server_for_buffer(buf);
    if (!srv || !srv->initialized) return;

    char *uri     = lsp_get_file_uri(buf->filename);
    char *content = lsp_build_content(buf);
    if (!uri || !content) { free(uri); free(content); return; }

    cJSON *params   = cJSON_CreateObject();
    cJSON *textdoc  = cJSON_CreateObject();
    cJSON_AddStringToObject(textdoc, "uri",     uri);
    cJSON_AddNumberToObject(textdoc, "version", g_doc_version++);
    cJSON_AddItemToObject(params, "textDocument", textdoc);

    /* Full-document sync: contentChanges is a single entry with the whole text */
    cJSON *changes = cJSON_CreateArray();
    cJSON *change  = cJSON_CreateObject();
    cJSON_AddStringToObject(change, "text", content);
    cJSON_AddItemToArray(changes, change);
    cJSON_AddItemToObject(params, "contentChanges", changes);

    lsp_send_notification(srv, "textDocument/didChange", params);
    free(uri); free(content);
}

/* ---------------------------------------------------- user-facing requests */

void lsp_request_hover(Buffer *buf, int line, int col) {
    if (!buf || !buf->filename) return;
    LspServer *srv = lsp_server_for_buffer(buf);
    if (!srv) {
        ed_set_status_message("LSP[%s]: no server (try :lsp_start)",
                              buf->filetype ? buf->filetype : "?");
        return;
    }
    if (!srv->initialized) {
        ed_set_status_message("LSP[%s]: still initializing, try again", srv->lang);
        return;
    }
    char *uri = lsp_get_file_uri(buf->filename);
    if (!uri) return;

    cJSON *params  = cJSON_CreateObject();
    cJSON *textdoc = cJSON_CreateObject();
    cJSON_AddStringToObject(textdoc, "uri", uri);
    cJSON_AddItemToObject(params, "textDocument", textdoc);
    cJSON *pos = cJSON_CreateObject();
    cJSON_AddNumberToObject(pos, "line",      line);
    cJSON_AddNumberToObject(pos, "character", col);
    cJSON_AddItemToObject(params, "position", pos);

    int id = srv->next_id++;
    lsp_pending_add(srv, id, LSP_REQ_HOVER);
    lsp_send_request(srv, "textDocument/hover", params, id);
    free(uri);
}

void lsp_request_definition(Buffer *buf, int line, int col) {
    if (!buf || !buf->filename) return;
    LspServer *srv = lsp_server_for_buffer(buf);
    if (!srv) {
        ed_set_status_message("LSP[%s]: no server (try :lsp_start)",
                              buf->filetype ? buf->filetype : "?");
        return;
    }
    if (!srv->initialized) {
        ed_set_status_message("LSP[%s]: still initializing, try again", srv->lang);
        return;
    }
    char *uri = lsp_get_file_uri(buf->filename);
    if (!uri) return;

    cJSON *params  = cJSON_CreateObject();
    cJSON *textdoc = cJSON_CreateObject();
    cJSON_AddStringToObject(textdoc, "uri", uri);
    cJSON_AddItemToObject(params, "textDocument", textdoc);
    cJSON *pos = cJSON_CreateObject();
    cJSON_AddNumberToObject(pos, "line",      line);
    cJSON_AddNumberToObject(pos, "character", col);
    cJSON_AddItemToObject(params, "position", pos);

    int id = srv->next_id++;
    lsp_pending_add(srv, id, LSP_REQ_DEFINITION);
    lsp_send_request(srv, "textDocument/definition", params, id);
    free(uri);
}

void lsp_request_completion(Buffer *buf, int line, int col) {
    if (!buf || !buf->filename) return;
    LspServer *srv = lsp_server_for_buffer(buf);
    if (!srv) {
        ed_set_status_message("LSP[%s]: no server (try :lsp_start)",
                              buf->filetype ? buf->filetype : "?");
        return;
    }
    if (!srv->initialized) {
        /* Defer until the server finishes its initialize handshake.
         * The most-recent request wins; rapid-fire C-Space is fine. */
        lsp_clear_deferred_completion();
        g_deferred_completion.lang    = strdup(srv->lang);
        g_deferred_completion.buf_idx = (int)(buf - E.buffers);
        g_deferred_completion.line    = line;
        g_deferred_completion.col     = col;
        ed_set_status_message("LSP[%s]: starting up, completion will appear shortly...",
                              srv->lang);
        return;
    }
    char *uri = lsp_get_file_uri(buf->filename);
    if (!uri) return;

    cJSON *params   = cJSON_CreateObject();
    cJSON *textdoc  = cJSON_CreateObject();
    cJSON_AddStringToObject(textdoc, "uri", uri);
    cJSON_AddItemToObject(params, "textDocument", textdoc);
    cJSON *pos = cJSON_CreateObject();
    cJSON_AddNumberToObject(pos, "line",      line);
    cJSON_AddNumberToObject(pos, "character", col);
    cJSON_AddItemToObject(params, "position", pos);

    int buf_idx = (int)(buf - E.buffers);
    int id      = srv->next_id++;
    lsp_pending_add_completion(srv, id, buf_idx, line, col);
    lsp_send_request(srv, "textDocument/completion", params, id);
    free(uri);
}

/* ------------------------------------------------ connect / disconnect */

static LspServer *lsp_server_alloc(const char *lang, const char *root_uri) {
    if (g_servers_count >= LSP_MAX_SERVERS) return NULL;
    LspServer *srv = calloc(1, sizeof(LspServer));
    if (!srv) return NULL;
    srv->lang           = strdup(lang);
    srv->root_uri       = strdup(root_uri ? root_uri : "file:///");
    srv->to_fd          = -1;
    srv->from_fd        = -1;
    srv->initialized    = 0;
    srv->next_id        = 1;
    srv->content_length = -1;
    for (int i = 0; i < LSP_MAX_SERVERS; i++) {
        if (!g_servers[i]) { g_servers[i] = srv; g_servers_count++; break; }
    }
    return srv;
}

/* Connect via named pipes.
 * to_path  : path hed writes to  (server reads — its stdin FIFO)
 * from_path: path hed reads from (server writes — its stdout FIFO)
 *
 * Both FIFOs must already exist and the server must already have them open.
 * Open O_RDWR to avoid blocking on a half-open FIFO.
 */
static int lsp_connect_pipe(LspServer *srv,
                             const char *to_path, const char *from_path) {
    srv->to_fd = open(to_path, O_RDWR);
    if (srv->to_fd < 0) {
        log_msg("LSP: open to_pipe %s: %s", to_path, strerror(errno));
        return -1;
    }
    srv->from_fd = open(from_path, O_RDWR);
    if (srv->from_fd < 0) {
        log_msg("LSP: open from_pipe %s: %s", from_path, strerror(errno));
        close(srv->to_fd); srv->to_fd = -1;
        return -1;
    }
    int fl = fcntl(srv->from_fd, F_GETFL, 0);
    fcntl(srv->from_fd, F_SETFL, fl | O_NONBLOCK);
    log_msg("LSP[%s]: connected via pipes %s / %s", srv->lang, to_path, from_path);
    return 0;
}

/* Connect via TCP socket to host:port. */
static int lsp_connect_tcp(LspServer *srv, const char *host, int port) {
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
        log_msg("LSP: getaddrinfo %s:%d failed: %s", host, port, strerror(errno));
        return -1;
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) { freeaddrinfo(res); return -1; }

    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        log_msg("LSP: connect %s:%d: %s", host, port, strerror(errno));
        close(sock); freeaddrinfo(res); return -1;
    }
    freeaddrinfo(res);

    int fl = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, fl | O_NONBLOCK);

    /* TCP is bidirectional; use the same fd for both directions. */
    srv->to_fd   = sock;
    srv->from_fd = sock;
    log_msg("LSP[%s]: connected via TCP %s:%d", srv->lang, host, port);
    return 0;
}

/* Spawn the server as a child process, wiring its stdio to two pipes. */
static int lsp_spawn_process(LspServer *srv, const char *const *argv) {
    int in_pipe[2];   /* editor → child stdin  */
    int out_pipe[2];  /* child  → editor stdout */
    if (pipe(in_pipe)  < 0) return -1;
    if (pipe(out_pipe) < 0) { close(in_pipe[0]); close(in_pipe[1]); return -1; }

    pid_t pid = fork();
    if (pid < 0) {
        close(in_pipe[0]);  close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        /* child */
        if (dup2(in_pipe[0],  STDIN_FILENO)  < 0) _exit(127);
        if (dup2(out_pipe[1], STDOUT_FILENO) < 0) _exit(127);
        /* Redirect stderr to .hedlog so server diagnostics (and any
         * "command not found"-style execvp fallout) are captured
         * instead of bleeding onto the editor's TTY. */
        int errfd = open(".hedlog", O_WRONLY | O_APPEND | O_CREAT, 0644);
        if (errfd >= 0) { dup2(errfd, STDERR_FILENO); close(errfd); }
        else            { int devnull = open("/dev/null", O_WRONLY);
                          if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); } }
        close(in_pipe[0]);  close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        execvp(argv[0], (char *const *)argv);
        /* execvp returned → not on $PATH or otherwise unrunnable. */
        const char *m1 = "[hed] execvp failed for ";
        write(STDERR_FILENO, m1, strlen(m1));
        write(STDERR_FILENO, argv[0], strlen(argv[0]));
        write(STDERR_FILENO, "\n", 1);
        _exit(127);
    }

    /* parent */
    close(in_pipe[0]);   /* child's stdin read end  */
    close(out_pipe[1]);  /* child's stdout write end */

    /* Detect immediate child death (e.g., execvp couldn't find the binary)
     * before we register the fd with the select loop. Short sleep then
     * non-blocking waitpid. */
    struct timespec ts = { 0, 50 * 1000 * 1000 }; /* 50 ms */
    nanosleep(&ts, NULL);
    int   wstatus = 0;
    pid_t r = waitpid(pid, &wstatus, WNOHANG);
    if (r == pid) {
        close(in_pipe[1]); close(out_pipe[0]);
        log_msg("LSP[%s]: child '%s' exited immediately (status=%d) — "
                "binary missing from $PATH? see .hedlog",
                srv->lang, argv[0], wstatus);
        return -1;
    }

    srv->pid     = pid;
    srv->to_fd   = in_pipe[1];
    srv->from_fd = out_pipe[0];

    int fl = fcntl(srv->from_fd, F_GETFL, 0);
    fcntl(srv->from_fd, F_SETFL, fl | O_NONBLOCK);

    log_msg("LSP[%s]: spawned %s (pid %d)", srv->lang, argv[0], (int)pid);
    return 0;
}

/* Walk up from `start` looking for any of `markers`. Returns 0 and writes
 * the absolute path of the first directory that contains a marker into
 * `out` (size `out_sz`); returns -1 if none found. */
static int lsp_find_root(const char *start, const char *const *markers,
                         char *out, size_t out_sz) {
    if (!start || !markers || !out || out_sz == 0) return -1;
    char dir[1024];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(dir, sizeof(dir), "%s", start);
#pragma GCC diagnostic pop
    /* If start is a file path, drop the trailing component. */
    struct stat st;
    if (stat(dir, &st) == 0 && S_ISREG(st.st_mode)) {
        char *slash = strrchr(dir, '/');
        if (slash && slash != dir) *slash = '\0';
        else if (slash == dir) dir[1] = '\0';
    }

    while (1) {
        for (int i = 0; markers[i]; i++) {
            char probe[2048];
            snprintf(probe, sizeof(probe), "%s/%s", dir, markers[i]);
            if (access(probe, F_OK) == 0) {
                snprintf(out, out_sz, "%s", dir);
                return 0;
            }
        }
        if (dir[0] == '/' && dir[1] == '\0') return -1;
        char *slash = strrchr(dir, '/');
        if (!slash) return -1;
        if (slash == dir) dir[1] = '\0';
        else *slash = '\0';
    }
}

/* :lsp_start <lang>  — spawn from the registry. If `hint_path` is non-NULL
 * it's used as the root-detection starting point (typically a buffer
 * filename); otherwise E.cwd is used. */
int lsp_cmd_start(const char *lang, const char *hint_path) {
    if (!lang || !*lang) {
        ed_set_status_message("LSP: usage: lsp_start <lang>");
        return -1;
    }
    if (lsp_server_for_lang(lang)) {
        ed_set_status_message("LSP[%s]: already running", lang);
        return -1;
    }
    const LspServerDef *def = lsp_servers_lookup(lang);
    if (!def) {
        ed_set_status_message("LSP[%s]: no entry in server registry", lang);
        return -1;
    }

    char root_dir[1024];
    if (lsp_find_root(hint_path && *hint_path ? hint_path : E.cwd,
                      def->root_markers, root_dir, sizeof(root_dir)) != 0) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        snprintf(root_dir, sizeof(root_dir), "%s", E.cwd);
#pragma GCC diagnostic pop
    }
    char root_uri[1100];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(root_uri, sizeof(root_uri), "file://%s", root_dir);
#pragma GCC diagnostic pop

    LspServer *srv = lsp_server_alloc(lang, root_uri);
    if (!srv) { ed_set_status_message("LSP: too many servers"); return -1; }

    if (lsp_spawn_process(srv, def->argv) < 0) {
        ed_set_status_message("LSP[%s]: spawn '%s' failed (is it installed?)",
                              lang, def->argv[0]);
        for (int i = 0; i < LSP_MAX_SERVERS; i++) {
            if (g_servers[i] == srv) { g_servers[i] = NULL; g_servers_count--; break; }
        }
        free(srv->lang); free(srv->root_uri); free(srv);
        return -1;
    }

    ed_loop_register(srv->lang, srv->from_fd, lsp_on_readable, srv);
    lsp_send_initialize(srv);
    ed_set_status_message("LSP[%s]: starting %s (root %s)",
                          lang, def->argv[0], root_dir);
    return 0;
}

int lsp_cmd_connect(const char *lang, const char *to_addr,
                    const char *from_addr, const char *root_uri) {
    if (!lang || !to_addr) {
        ed_set_status_message("LSP: usage: lsp_connect <lang> tcp <host>:<port>  "
                              "or  lsp_connect <lang> <to_pipe> <from_pipe>");
        return -1;
    }

    if (lsp_server_for_lang(lang)) {
        ed_set_status_message("LSP[%s]: already connected (use lsp_disconnect first)", lang);
        return -1;
    }

    char resolved_root[1024];
    /* Truncation OK: an over-long root URI just gets clipped — the LSP
     * server will reject it on initialize and we'll surface the error. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    if (root_uri) {
        snprintf(resolved_root, sizeof(resolved_root), "%s", root_uri);
    } else {
        snprintf(resolved_root, sizeof(resolved_root), "file://%s", E.cwd);
    }
#pragma GCC diagnostic pop

    LspServer *srv = lsp_server_alloc(lang, resolved_root);
    if (!srv) {
        ed_set_status_message("LSP: too many servers");
        return -1;
    }

    int ok = -1;

    if (strcmp(to_addr, "tcp") == 0) {
        /* TCP mode: from_addr is "host:port" */
        if (!from_addr) {
            ed_set_status_message("LSP: tcp mode needs host:port argument");
            goto fail;
        }
        char host[256];
        int  port = 0;
        /* parse last colon as separator so IPv6 addrs work */
        const char *colon = strrchr(from_addr, ':');
        if (!colon) {
            ed_set_status_message("LSP: invalid address '%s' (expected host:port)", from_addr);
            goto fail;
        }
        size_t hlen = (size_t)(colon - from_addr);
        if (hlen >= sizeof(host)) hlen = sizeof(host) - 1;
        memcpy(host, from_addr, hlen); host[hlen] = '\0';
        port = atoi(colon + 1);
        if (port <= 0 || port > 65535) {
            ed_set_status_message("LSP: invalid port in '%s'", from_addr);
            goto fail;
        }
        ok = lsp_connect_tcp(srv, host, port);
    } else {
        /* Pipe mode: to_addr is the write pipe, from_addr is the read pipe */
        if (!from_addr) {
            ed_set_status_message("LSP: pipe mode needs two paths");
            goto fail;
        }
        ok = lsp_connect_pipe(srv, to_addr, from_addr);
    }

    if (ok < 0) {
        ed_set_status_message("LSP[%s]: connection failed (check log)", lang);
        goto fail;
    }

    ed_loop_register(srv->lang, srv->from_fd, lsp_on_readable, srv);

    lsp_send_initialize(srv);
    ed_set_status_message("LSP[%s]: connecting...", lang);
    return 0;

fail:
    for (int i = 0; i < LSP_MAX_SERVERS; i++) {
        if (g_servers[i] == srv) { g_servers[i] = NULL; g_servers_count--; break; }
    }
    lsp_close_fds(srv);
    free(srv->lang); free(srv->root_uri); free(srv);
    return -1;
}

int lsp_cmd_disconnect(const char *lang) {
    if (!lang) { ed_set_status_message("LSP: specify a language"); return -1; }
    LspServer *srv = lsp_server_for_lang(lang);
    if (!srv) { ed_set_status_message("LSP[%s]: not connected", lang); return -1; }

    char lang_copy[64];
    snprintf(lang_copy, sizeof(lang_copy), "%s", srv->lang);
    lsp_close_fds(srv);
    free(srv->lang); free(srv->root_uri); free(srv->msg_body); free(srv);
    for (int i = 0; i < LSP_MAX_SERVERS; i++) {
        if (g_servers[i] == srv) { g_servers[i] = NULL; g_servers_count--; break; }
    }
    ed_set_status_message("LSP[%s]: disconnected", lang_copy);
    return 0;
}

void lsp_cmd_status(void) {
    if (g_servers_count == 0) {
        ed_set_status_message("LSP: no servers connected");
        return;
    }
    char buf[512];
    int  off = 0;
    off += snprintf(buf + off, sizeof(buf) - (size_t)off, "LSP:");
    for (int i = 0; i < LSP_MAX_SERVERS; i++) {
        LspServer *srv = g_servers[i];
        if (!srv) continue;
        off += snprintf(buf + off, sizeof(buf) - (size_t)off,
                        " [%s %s fd=%d/%d]",
                        srv->lang,
                        srv->initialized ? "ready" : "init",
                        srv->to_fd, srv->from_fd);
    }
    ed_set_status_message("%s", buf);
}
