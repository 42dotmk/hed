#include "completion/completion.h"
#include "hed.h"
#include "json_helpers.h"
#include "jsonrpc/jsonrpc.h"
#include "lsp.h"
#include "lsp_hooks.h"
#include "lsp_servers.h"
#include "select_loop.h"
#include "utils/quickfix.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>

#define LSP_MAX_SERVERS 8
#define LSP_PENDING_MAX 32

typedef enum {
    LSP_REQ_NONE = 0,
    LSP_REQ_HOVER,
    LSP_REQ_DEFINITION,
    LSP_REQ_COMPLETION,
} LspReqKind;

typedef struct {
    int id;
    LspReqKind kind;
    /* For LSP_REQ_COMPLETION: the buffer + cursor position at request
     * time, so the response handler can compute the replacement range
     * even if the user kept typing while waiting. */
    int buf_idx;
    int req_line;
    int req_col;
    /* Completion-menu generation token (see completion_provide). */
    unsigned token;
} LspPending;

struct LspServer {
    char *lang;
    char *root_uri;

    pid_t pid; /* child PID if we spawned it; 0 if attached via :lsp_connect */

    int to_fd;   /* editor writes here  (server stdin)  */
    int from_fd; /* editor reads from here (server stdout) */

    int initialized;
    int initialize_id; /* request id of the initialize handshake */
    int next_id;

    /* completionProvider.triggerCharacters from the initialize result,
     * flattened to a set of single bytes ('.', ':', '>', ...). */
    char trigger_chars[32];

    /* Incoming message framing */
    JrpcReader reader;

    LspPending pending[LSP_PENDING_MAX];
};

static LspServer *g_servers[LSP_MAX_SERVERS];
static int g_servers_count = 0;

/* Monotonically increasing document version counter.
 * LSP requires per-document versions to be strictly increasing;
 * a global counter that only ever grows satisfies that requirement. */
static int g_doc_version = 1;

/* ----- diagnostics store -------------------------------------------
 * Latest publishDiagnostics per-URI. Replaced wholesale every time the
 * server re-publishes for a file. Survives across :lsp_disconnect, so
 * the user can inspect the last known state. */

typedef struct {
    int line; /* 0-based, LSP coords */
    int col;
    int severity; /* 1=Error, 2=Warning, 3=Info, 4=Hint */
    char *message;
} LspDiag;

typedef struct {
    char *uri;
    LspDiag *items; /* stb_ds */
} LspDiagFile;

static LspDiagFile *g_diags = NULL; /* stb_ds */

/* ------------------------------------------------------------------ helpers */

static LspServer *lsp_server_for_lang(const char *lang) {
    if (!lang)
        return NULL;
    for (int i = 0; i < LSP_MAX_SERVERS; i++) {
        if (g_servers[i] && strcmp(g_servers[i]->lang, lang) == 0)
            return g_servers[i];
    }
    return NULL;
}

static LspServer *lsp_server_for_buffer(Buffer *buf) {
    if (!buf || !buf->filetype)
        return NULL;
    return lsp_server_for_lang(buf->filetype);
}

/* Build full document text from buffer rows (not from disk). */
static char *lsp_get_file_uri(const char *filepath) {
    return fs_path_to_file_uri(filepath, NULL);
}

/* ------------------------------------------------- UTF-16 positions
 * LSP `character` offsets are UTF-16 code units; hed columns are byte
 * offsets. 1-3 byte UTF-8 sequences are one UTF-16 unit, 4-byte
 * sequences (astral plane) are a surrogate pair = two units.
 * Continuation bytes (0x80-0xBF) count zero. */

static int lsp_cx_to_utf16(const char *s, size_t len, int cx) {
    int u = 0;
    if (cx > (int)len)
        cx = (int)len;
    for (int i = 0; i < cx && s; i++) {
        unsigned char c = (unsigned char)s[i];
        if ((c & 0xC0) == 0x80)
            continue; /* continuation byte */
        u += (c >= 0xF0) ? 2 : 1;
    }
    return u;
}

static int lsp_utf16_to_cx(const char *s, size_t len, int u16) {
    int u = 0;
    int i = 0;
    while (i < (int)len && s) {
        unsigned char c = (unsigned char)s[i];
        int adv = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
        int units = (c >= 0xF0) ? 2 : 1;
        if (u + units > u16)
            break;
        u += units;
        i += adv;
        if (u >= u16)
            break;
    }
    return i;
}

/* ------------------------------------------------- document sync
 * Full-text didChange, sent lazily: each buffer remembers the undo
 * generation + dirty counter of its last sync and a fresh didChange
 * goes out only when either moved (undo/redo bumps dirty but not the
 * generation). Keyed by filename — buffer indices shift on close. */

typedef struct {
    char *filename;
    unsigned long gen;
    int dirty;
} LspSyncEnt;

static LspSyncEnt *g_sync = NULL; /* stb_ds */

static LspSyncEnt *lsp_sync_slot(const char *filename, int create) {
    for (ptrdiff_t i = 0; i < arrlen(g_sync); i++) {
        if (strcmp(g_sync[i].filename, filename) == 0)
            return &g_sync[i];
    }
    if (!create)
        return NULL;
    LspSyncEnt e = {.filename = strdup(filename), .gen = 0, .dirty = -1};
    arrput(g_sync, e);
    return &g_sync[arrlen(g_sync) - 1];
}

static void lsp_sync_forget(const char *filename) {
    if (!filename)
        return;
    for (ptrdiff_t i = 0; i < arrlen(g_sync); i++) {
        if (strcmp(g_sync[i].filename, filename) == 0) {
            free(g_sync[i].filename);
            arrdel(g_sync, i);
            return;
        }
    }
}

static void lsp_sync_free(void) {
    for (ptrdiff_t i = 0; i < arrlen(g_sync); i++)
        free(g_sync[i].filename);
    arrfree(g_sync);
    g_sync = NULL;
}

/* -------------------------------------------------- pending request table */

static void lsp_pending_add(LspServer *srv, int id, LspReqKind kind) {
    for (int i = 0; i < LSP_PENDING_MAX; i++) {
        if (srv->pending[i].kind == LSP_REQ_NONE) {
            srv->pending[i] = (LspPending){.id = id, .kind = kind};
            return;
        }
    }
    log_msg("LSP: pending table full, dropping request id=%d", id);
}

static void lsp_pending_add_completion(LspServer *srv, int id, int buf_idx,
                                       int line, int col, unsigned token) {
    for (int i = 0; i < LSP_PENDING_MAX; i++) {
        if (srv->pending[i].kind == LSP_REQ_NONE) {
            srv->pending[i] = (LspPending){
                .id = id,
                .kind = LSP_REQ_COMPLETION,
                .buf_idx = buf_idx,
                .req_line = line,
                .req_col = col,
                .token = token,
            };
            return;
        }
    }
    log_msg("LSP: pending table full, dropping request id=%d", id);
}

static LspPending lsp_pending_pop(LspServer *srv, int id) {
    LspPending empty = {.kind = LSP_REQ_NONE};
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

static void lsp_send_request(LspServer *srv, const char *method, cJSON *params,
                             int req_id) {
    if (!srv || srv->to_fd < 0) {
        if (params)
            cJSON_Delete(params);
        return;
    }
    if (jrpc_send(srv->to_fd, jrpc_request(method, params, req_id)) < 0)
        log_msg("LSP[%s]: write failed: %s", srv->lang, strerror(errno));
    log_msg("LSP[%s]: → %s id=%d", srv->lang, method, req_id);
}

static void lsp_send_notification(LspServer *srv, const char *method,
                                  cJSON *params) {
    if (!srv || srv->to_fd < 0) {
        if (params)
            cJSON_Delete(params);
        return;
    }
    if (jrpc_send(srv->to_fd, jrpc_notification(method, params)) < 0)
        log_msg("LSP[%s]: write failed: %s", srv->lang, strerror(errno));
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
    cJSON *caps = cJSON_CreateObject();
    cJSON *tdoc = cJSON_CreateObject();

    cJSON *sync = cJSON_CreateObject();
    cJSON_AddBoolToObject(sync, "dynamicRegistration", 0);
    cJSON_AddBoolToObject(sync, "willSave", 0);
    cJSON_AddBoolToObject(sync, "willSaveWaitUntil", 0);
    cJSON_AddBoolToObject(sync, "didSave", 1);
    cJSON_AddItemToObject(tdoc, "synchronization", sync);

    cJSON *hover = cJSON_CreateObject();
    cJSON_AddBoolToObject(hover, "dynamicRegistration", 0);
    cJSON *hfmt = cJSON_CreateArray();
    cJSON_AddItemToArray(hfmt, cJSON_CreateString("plaintext"));
    cJSON_AddItemToArray(hfmt, cJSON_CreateString("markdown"));
    cJSON_AddItemToObject(hover, "contentFormat", hfmt);
    cJSON_AddItemToObject(tdoc, "hover", hover);

    cJSON *def = cJSON_CreateObject();
    cJSON_AddBoolToObject(def, "dynamicRegistration", 0);
    cJSON_AddItemToObject(tdoc, "definition", def);

    cJSON *comp = cJSON_CreateObject();
    cJSON_AddBoolToObject(comp, "dynamicRegistration", 0);
    cJSON_AddItemToObject(tdoc, "completion", comp);

    cJSON_AddItemToObject(caps, "textDocument", tdoc);
    cJSON_AddItemToObject(params, "capabilities", caps);

    int id = srv->next_id++;
    srv->initialize_id = id;
    lsp_send_request(srv, "initialize", params, id);
}

/* Pull the bits of ServerCapabilities we act on out of the initialize
 * result: completion trigger characters (used by the completion
 * plugin's auto-trigger). */
static void lsp_parse_capabilities(LspServer *srv, cJSON *result) {
    srv->trigger_chars[0] = '\0';
    cJSON *caps = result ? json_get_object(result, "capabilities") : NULL;
    cJSON *comp = caps ? json_get_object(caps, "completionProvider") : NULL;
    cJSON *trig = comp ? json_get_array(comp, "triggerCharacters") : NULL;
    if (!trig)
        return;
    size_t w = 0;
    for (int i = 0; i < cJSON_GetArraySize(trig); i++) {
        cJSON *t = cJSON_GetArrayItem(trig, i);
        if (cJSON_IsString(t) && t->valuestring && t->valuestring[0] &&
            w + 1 < sizeof(srv->trigger_chars)) {
            /* Multi-byte trigger strings ("->", "::"): their last byte
             * is the one that arrives right before completion fires. */
            const char *s = t->valuestring;
            srv->trigger_chars[w++] = s[strlen(s) - 1];
        }
    }
    srv->trigger_chars[w] = '\0';
    log_msg("LSP[%s]: completion triggers: \"%s\"", srv->lang,
            srv->trigger_chars);
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

/* ------------------------------------------ response / notification handlers
 */

/* Strip basic markdown inline markers in-place: **x** *x* `x` -> x */
static void strip_markdown_inline(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if (r[0] == '*' && r[1] == '*') {
            r += 2;
            continue;
        }
        if (r[0] == '*') {
            r += 1;
            continue;
        }
        if (r[0] == '`') {
            r += 1;
            continue;
        }
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

    /* First pass: split lines, skip bare file paths, count content lines.
     * A "bare path" is a line whose first non-space char is '/' and contains
     * no spaces — clangd appends these after the actual hover content. */
    int nlines = 0;
    char src_label[256] = {0}; /* basename of the last skipped path line */
    const char *p = text;
    while (*p) {
        int len = 0;
        while (p[len] && p[len] != '\n')
            len++;
        int w = len;
        while (w > 0 && (p[w - 1] == '\r' || p[w - 1] == ' '))
            w--;
        /* detect bare path: starts with '/', no spaces */
        int is_path = (w > 0 && p[0] == '/');
        if (is_path) {
            for (int i = 1; i < w; i++) {
                if (p[i] == ' ') {
                    is_path = 0;
                    break;
                }
            }
        }
        if (is_path) {
            /* extract basename for bottom label */
            const char *slash = p;
            for (int i = 0; i < w; i++)
                if (p[i] == '/')
                    slash = p + i + 1;
            int blen = (int)(p + w - slash);
            if (blen > (int)sizeof(src_label) - 1)
                blen = (int)sizeof(src_label) - 1;
            memcpy(src_label, slash, (size_t)blen);
            src_label[blen] = '\0';
        } else {
            nlines++;
        }
        p += len + (p[len] == '\n' ? 1 : 0);
    }
    /* strip trailing blank content lines */
    if (nlines == 0) {
        ed_set_status_message("LSP: (empty)");
        return;
    }

    /* Scratch buffer — no filename (no top title), title = src label */
    BufSpecial spec = {.title = src_label[0] ? src_label : NULL, .readonly = 1};
    int buf_idx = buf_special_get(&spec, NULL);
    if (buf_idx < 0)
        return;
    Buffer *buf = &E.buffers[buf_idx];

    /* Second pass: insert non-path lines into the buffer */
    p = text;
    int row = 0;
    while (*p) {
        int len = 0;
        while (p[len] && p[len] != '\n')
            len++;
        char *line = malloc((size_t)len + 1);
        if (!line)
            break;
        memcpy(line, p, (size_t)len);
        line[len] = '\0';
        /* strip \r */
        int w = len;
        if (w > 0 && line[w - 1] == '\r')
            line[--w] = '\0';
        /* skip bare path lines */
        int is_path = (w > 0 && line[0] == '/');
        if (is_path) {
            for (int i = 1; i < w; i++) {
                if (line[i] == ' ') {
                    is_path = 0;
                    break;
                }
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
    /* Show as a centered modal sized from the content. */
    Window *modal = buf_special_show_modal(buf_idx, -1, -1);
    if (!modal)
        return;
    lsp_popup_track(modal);
    ed_set_status_message("q/<Esc> close  j/k scroll");
}

/* ----- completion ------------------------------------------------- */

/* LSP CompletionItemKind (1-25) → the menu's coarse kinds. */
static CmpKind lsp_kind_to_cmp(int k) {
    switch (k) {
    case 1:
        return CMP_KIND_TEXT;
    case 2: /* Method */
    case 3: /* Function */
    case 4: /* Constructor */
        return CMP_KIND_FN;
    case 5:  /* Field */
    case 10: /* Property */
        return CMP_KIND_FIELD;
    case 6: /* Variable */
        return CMP_KIND_VAR;
    case 7:  /* Class */
    case 8:  /* Interface */
    case 13: /* Enum */
    case 22: /* Struct */
    case 25: /* TypeParameter */
        return CMP_KIND_TYPE;
    case 9: /* Module */
        return CMP_KIND_MOD;
    case 14: /* Keyword */
        return CMP_KIND_KW;
    case 15: /* Snippet */
        return CMP_KIND_SNIP;
    case 20: /* EnumMember */
    case 21: /* Constant */
        return CMP_KIND_CONST;
    default:
        return CMP_KIND_OTHER;
    }
}

/* Strip snippet placeholders in place: "$0" / "$1" drop, "${1:text}"
 * keeps "text". Good enough for insertTextFormat == 2 servers. */
static void lsp_strip_snippet(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if (*r != '$') {
            *w++ = *r++;
            continue;
        }
        r++;
        if (*r == '{') {
            r++;
            while (*r && *r != ':' && *r != '}')
                r++;
            if (*r == ':') {
                r++;
                while (*r && *r != '}')
                    *w++ = *r++;
            }
            if (*r == '}')
                r++;
        } else {
            while (*r && isdigit((unsigned char)*r))
                r++;
        }
    }
    *w = '\0';
}

/* Convert a completion response into CmpItems for the completion
 * plugin's menu. `pop` carries the request position (for textEdit
 * range conversion) and the menu token. */
static void lsp_handle_completion_result(LspServer *srv, const LspPending *pop,
                                         cJSON *result) {
    /* The request row's text, for UTF-16 → byte column conversion of
     * textEdit ranges. May legitimately be gone (buffer closed). */
    const char *rowtext = NULL;
    size_t rowlen = 0;
    if (pop->buf_idx >= 0 && pop->buf_idx < (int)arrlen(E.buffers)) {
        Buffer *buf = &E.buffers[pop->buf_idx];
        if (pop->req_line >= 0 && pop->req_line < buf->num_rows) {
            rowtext = buf->rows[pop->req_line].chars.data;
            rowlen = buf->rows[pop->req_line].chars.len;
        }
    }

    cJSON *items = NULL;
    if (result && !cJSON_IsNull(result))
        items =
            cJSON_IsArray(result) ? result : json_get_array(result, "items");
    int n = items ? cJSON_GetArraySize(items) : 0;
    if (n > 200)
        n = 200;
    if (n <= 0) {
        completion_provide(pop->token, NULL, 0);
        return;
    }

    CmpItem *out = calloc((size_t)n, sizeof(CmpItem));
    if (!out)
        return;
    int kept = 0;
    for (int i = 0; i < n; i++) {
        cJSON *it = cJSON_GetArrayItem(items, i);
        const char *label = json_get_string(it, "label");
        if (!label || !*label)
            continue;
        CmpItem *o = &out[kept];
        o->label = strdup(label);
        o->kind = lsp_kind_to_cmp(json_get_int(it, "kind", 0));
        o->edit_start = -1;
        o->edit_end = -1;

        const char *detail = json_get_string(it, "detail");
        if (!detail || !*detail) {
            cJSON *ld = json_get_object(it, "labelDetails");
            detail = ld ? json_get_string(ld, "description") : NULL;
        }
        if (detail && *detail)
            o->detail = strdup(detail);

        const char *sort = json_get_string(it, "sortText");
        if (sort && *sort)
            o->sort_text = strdup(sort);

        /* Prefer textEdit.newText + range; fall back to insertText. */
        const char *ins = NULL;
        cJSON *edit = json_get_object(it, "textEdit");
        if (edit) {
            ins = json_get_string(edit, "newText");
            cJSON *range = json_get_object(edit, "range");
            if (!range) /* InsertReplaceEdit */
                range = json_get_object(edit, "insert");
            cJSON *start = range ? json_get_object(range, "start") : NULL;
            cJSON *end = range ? json_get_object(range, "end") : NULL;
            if (start && end && rowtext &&
                json_get_int(start, "line", -1) == pop->req_line) {
                o->edit_start = lsp_utf16_to_cx(
                    rowtext, rowlen, json_get_int(start, "character", 0));
                o->edit_end = lsp_utf16_to_cx(
                    rowtext, rowlen, json_get_int(end, "character", 0));
            }
        }
        if (!ins || !*ins)
            ins = json_get_string(it, "insertText");
        if (ins && *ins && strcmp(ins, label) != 0)
            o->insert_text = strdup(ins);
        if (json_get_int(it, "insertTextFormat", 1) == 2) {
            if (!o->insert_text)
                o->insert_text = strdup(ins && *ins ? ins : label);
            lsp_strip_snippet(o->insert_text);
        }
        kept++;
    }
    (void)srv;
    completion_provide(pop->token, out, kept);
}

static void lsp_handle_hover_result(cJSON *result) {
    if (!result || cJSON_IsNull(result)) {
        ed_set_status_message("LSP hover: no info");
        return;
    }

    /* contents: string | { kind, value } | MarkedString[] */
    cJSON *contents = cJSON_GetObjectItemCaseSensitive(result, "contents");
    const char *text = NULL;

    if (cJSON_IsString(contents)) {
        text = contents->valuestring;
    } else if (cJSON_IsObject(contents)) {
        cJSON *val = cJSON_GetObjectItemCaseSensitive(contents, "value");
        if (val && cJSON_IsString(val))
            text = val->valuestring;
    } else if (cJSON_IsArray(contents)) {
        /* MarkedString[]: concatenate all entries */
        StrBuf combined = strbuf_new();
        for (int i = 0; i < cJSON_GetArraySize(contents); i++) {
            cJSON *item = cJSON_GetArrayItem(contents, i);
            const char *v = NULL;
            if (cJSON_IsString(item))
                v = item->valuestring;
            else {
                cJSON *val = cJSON_GetObjectItemCaseSensitive(item, "value");
                if (val && cJSON_IsString(val))
                    v = val->valuestring;
            }
            if (v && *v) {
                if (combined.len > 0)
                    strbuf_append_char(&combined, '\n');
                strbuf_append(&combined, v, strlen(v));
            }
        }
        if (combined.len > 0) {
            lsp_show_popup("Hover", combined.data);
            strbuf_free(&combined);
            return;
        }
        strbuf_free(&combined);
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
    if (!uri)
        uri = json_get_string(loc, "targetUri"); /* LocationLink */
    if (!uri) {
        ed_set_status_message("LSP: definition missing uri");
        return;
    }

    cJSON *range = json_get_object(loc, "range");
    if (!range)
        range = json_get_object(loc, "targetSelectionRange");
    int line = 0, col = 0;
    if (range) {
        cJSON *start = json_get_object(range, "start");
        if (start) {
            line = json_get_int(start, "line", 0);
            col = json_get_int(start, "character", 0);
        }
    }

    const char *path = fs_uri_to_path(uri);
    log_msg("LSP definition: %s:%d:%d", path, line + 1, col + 1);

    /* Explicit jump-list push: buf_open_or_switch only records when
     * switching buffers, which would skip same-file jumps. */
    kb_jump_save_current();
    buf_open_or_switch(path, false);
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (buf && buf->num_rows > 0) {
        if (line >= buf->num_rows)
            line = buf->num_rows - 1;
        if (line < 0)
            line = 0;
        Row *row = &buf->rows[line];
        int cx = lsp_utf16_to_cx(row->chars.data, row->chars.len, col);
        if (cx > (int)row->chars.len)
            cx = (int)row->chars.len;
        buf->cursor->y = line;
        buf->cursor->x = cx;
        if (win && !win->is_modal) {
            win->cursor.y = line;
            win->cursor.x = cx;
        }
        buf_center_screen();
    }
    ed_set_status_message("LSP: jumped to %s:%d", path, line + 1);
}

static void lsp_process_response(LspServer *srv, cJSON *json) {
    cJSON *id_node = cJSON_GetObjectItemCaseSensitive(json, "id");
    int id = id_node ? (int)id_node->valuedouble : -1;

    cJSON *error = cJSON_GetObjectItemCaseSensitive(json, "error");
    if (error) {
        const char *msg = json_get_string(error, "message");
        log_msg("LSP[%s]: error id=%d: %s", srv->lang, id, msg ? msg : "?");
        ed_set_status_message("LSP error: %s", msg ? msg : "unknown");
        LspPending pop = lsp_pending_pop(srv, id);
        if (pop.kind == LSP_REQ_COMPLETION)
            completion_provide(pop.token, NULL, 0); /* release the menu */
        return;
    }

    cJSON *result = cJSON_GetObjectItemCaseSensitive(json, "result");

    /* initialize response */
    if (!srv->initialized && id == srv->initialize_id) {
        srv->initialize_id = -1;
        lsp_parse_capabilities(srv, result);
        lsp_send_initialized(srv);
        lsp_notify_existing_buffers(srv);
        return;
    }

    LspPending pop = lsp_pending_pop(srv, id);
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
        if (strcmp(g_diags[i].uri, uri) == 0)
            return &g_diags[i];
    }
    LspDiagFile slot = {.uri = strdup(uri), .items = NULL};
    arrput(g_diags, slot);
    return &g_diags[arrlen(g_diags) - 1];
}

static void lsp_diag_clear_slot(LspDiagFile *slot) {
    for (ptrdiff_t i = 0; i < arrlen(slot->items); i++)
        free(slot->items[i].message);
    /* arrsetlen(a, 0) trips -Wtype-limits; delete instead. */
    if (arrlen(slot->items))
        arrdeln(slot->items, 0, arrlen(slot->items));
}

/* Replace the diagnostics for `uri` with the LSP `diag` array. */
static void lsp_diag_replace(const char *uri, cJSON *diag, int n) {
    LspDiagFile *slot = lsp_diag_slot(uri);
    lsp_diag_clear_slot(slot);
    for (int i = 0; i < n; i++) {
        cJSON *d = cJSON_GetArrayItem(diag, i);
        cJSON *range = json_get_object(d, "range");
        cJSON *start = range ? json_get_object(range, "start") : NULL;
        int line = start ? json_get_int(start, "line", 0) : 0;
        int col = start ? json_get_int(start, "character", 0) : 0;
        int sev = json_get_int(d, "severity", 1);
        const char *msg = json_get_string(d, "message");
        LspDiag e = {
            .line = line,
            .col = col,
            .severity = sev,
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
        const char *fp = fs_uri_to_path(df->uri);
        for (ptrdiff_t i = 0; i < arrlen(df->items); i++) {
            LspDiag *d = &df->items[i];
            const char *sev = (d->severity == 1)   ? "E"
                              : (d->severity == 2) ? "W"
                              : (d->severity == 3) ? "I"
                                                   : "H";
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
    if (!method)
        return;
    log_msg("LSP[%s]: ← %s", srv->lang, method);

    if (strcmp(method, "textDocument/publishDiagnostics") == 0) {
        cJSON *params = json_get_object(json, "params");
        if (!params)
            return;
        const char *uri = json_get_string(params, "uri");
        cJSON *diag = json_get_array(params, "diagnostics");
        int cnt = diag ? cJSON_GetArraySize(diag) : 0;
        log_msg("LSP[%s]: diagnostics for %s: %d items", srv->lang,
                uri ? uri : "?", cnt);
        if (uri)
            lsp_diag_replace(uri, diag, cnt);
    } else if (strcmp(method, "window/showMessage") == 0) {
        cJSON *params = json_get_object(json, "params");
        const char *msg = params ? json_get_string(params, "message") : NULL;
        if (msg)
            ed_set_status_message("LSP: %s", msg);
    } else if (strcmp(method, "window/logMessage") == 0) {
        cJSON *params = json_get_object(json, "params");
        const char *msg = params ? json_get_string(params, "message") : NULL;
        if (msg)
            log_msg("LSP[%s] server log: %s", srv->lang, msg);
    }
}

/* Server→client REQUESTS (id + method) must be answered or servers
 * that block on them (rust-analyzer, lua-language-server) stall the
 * whole session. We decline everything gracefully. */
static void lsp_process_server_request(LspServer *srv, cJSON *json,
                                       const cJSON *id, const char *method) {
    log_msg("LSP[%s]: ←? %s (server request)", srv->lang, method);
    cJSON *reply;
    if (strcmp(method, "workspace/configuration") == 0) {
        /* One null per requested item: "no configuration". */
        cJSON *result = cJSON_CreateArray();
        cJSON *params = json_get_object(json, "params");
        cJSON *items = params ? json_get_array(params, "items") : NULL;
        int n = items ? cJSON_GetArraySize(items) : 0;
        for (int i = 0; i < n; i++)
            cJSON_AddItemToArray(result, cJSON_CreateNull());
        reply = jrpc_response(id, result);
    } else if (strcmp(method, "client/registerCapability") == 0 ||
               strcmp(method, "client/unregisterCapability") == 0 ||
               strcmp(method, "window/workDoneProgress/create") == 0 ||
               strcmp(method, "window/showMessageRequest") == 0 ||
               strcmp(method, "workspace/workspaceFolders") == 0) {
        reply = jrpc_response(id, cJSON_CreateNull());
    } else {
        reply = jrpc_error(id, -32601, "method not found");
    }
    if (jrpc_send(srv->to_fd, reply) < 0)
        log_msg("LSP[%s]: write failed: %s", srv->lang, strerror(errno));
}

static void lsp_handle_message(LspServer *srv, const char *msg, int len) {
    log_msg("LSP[%s]: message len=%d: %.120s", srv->lang, len, msg);
    cJSON *json = json_parse(msg, (size_t)len);
    if (!json) {
        log_msg("LSP: JSON parse error");
        return;
    }

    cJSON *id = cJSON_GetObjectItemCaseSensitive(json, "id");
    const char *method = json_get_string(json, "method");
    if (id && !cJSON_IsNull(id) && method)
        lsp_process_server_request(srv, json, id, method);
    else if (id && !cJSON_IsNull(id))
        lsp_process_response(srv, json);
    else
        lsp_process_notification(srv, json);

    cJSON_Delete(json);
}

/* -------------------------------------------------------- public API:
 * lifecycle */

void lsp_init(void) {
    for (int i = 0; i < LSP_MAX_SERVERS; i++)
        g_servers[i] = NULL;
    g_servers_count = 0;
    log_msg("LSP: init");
}

static void lsp_on_readable(int fd, void *ud);

static void lsp_reap_child(LspServer *srv) {
    if (srv->pid <= 0)
        return;
    /* Closing stdin should make most LSPs exit; give them a moment, then
     * SIGTERM, then reap. WNOHANG keeps us non-blocking. */
    for (int i = 0; i < 5; i++) {
        int status = 0;
        pid_t r = waitpid(srv->pid, &status, WNOHANG);
        if (r == srv->pid || r < 0) {
            srv->pid = 0;
            return;
        }
        if (i == 1)
            kill(srv->pid, SIGTERM);
        if (i == 3)
            kill(srv->pid, SIGKILL);
        struct timespec ts = {0, 20 * 1000 * 1000}; /* 20ms */
        nanosleep(&ts, NULL);
    }
    srv->pid = 0;
}

static void lsp_close_fds(LspServer *srv) {
    if (srv->from_fd >= 0)
        ed_loop_unregister(srv->from_fd);
    if (srv->to_fd >= 0)
        close(srv->to_fd);
    if (srv->from_fd >= 0 && srv->from_fd != srv->to_fd)
        close(srv->from_fd);
    srv->to_fd = srv->from_fd = -1;
    lsp_reap_child(srv);
}

/* The single teardown path: clear the registry slot, close fds (which
 * unregisters from the select loop and reaps a spawned child), free.
 * Safe on servers in any state, including half-connected ones. */
static void lsp_server_remove(LspServer *srv) {
    if (!srv)
        return;
    for (int i = 0; i < LSP_MAX_SERVERS; i++) {
        if (g_servers[i] == srv) {
            g_servers[i] = NULL;
            g_servers_count--;
            break;
        }
    }
    lsp_close_fds(srv);
    free(srv->lang);
    free(srv->root_uri);
    jrpc_reader_free(&srv->reader);
    free(srv);
}

/* Protocol-polite teardown: shutdown request + exit notification, then
 * remove. We don't wait for the shutdown response — closing stdin plus
 * lsp_reap_child's escalating SIGTERM/SIGKILL loop bounds the exit. */
static void lsp_server_disconnect_polite(LspServer *srv) {
    if (srv->to_fd >= 0 && srv->initialized) {
        lsp_send_request(srv, "shutdown", NULL, srv->next_id++);
        lsp_send_notification(srv, "exit", NULL);
    }
    lsp_server_remove(srv);
}

static void lsp_diags_free(void) {
    for (ptrdiff_t f = 0; f < arrlen(g_diags); f++) {
        lsp_diag_clear_slot(&g_diags[f]);
        arrfree(g_diags[f].items);
        free(g_diags[f].uri);
    }
    arrfree(g_diags);
    g_diags = NULL;
}

/* Full plugin teardown: polite-disconnect every server, free stores.
 * Idempotent — runs from plugin deinit and from atexit (cmd_quit exits
 * the process directly). */
void lsp_shutdown(void) {
    for (int i = 0; i < LSP_MAX_SERVERS; i++) {
        if (g_servers[i])
            lsp_server_disconnect_polite(g_servers[i]);
    }
    g_servers_count = 0;
    lsp_diags_free();
    lsp_sync_free();
}

static void lsp_on_readable(int fd, void *ud) {
    LspServer *srv = ud;
    if (!srv || srv->from_fd != fd)
        return;

    char tmp[65536];
    ssize_t n = read(srv->from_fd, tmp, sizeof(tmp));
    if (n <= 0) {
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return;
        log_msg("LSP[%s]: disconnected", srv->lang);
        char lang_copy[64];
        snprintf(lang_copy, sizeof(lang_copy), "%s", srv->lang);
        int was_spawned = srv->pid > 0;
        if (was_spawned) {
            /* We owned the child; drop the record so auto-start can retry
             * on the next buffer open. Manually-attached servers (pid==0)
             * stay registered so the user can reconnect to the same one. */
            lsp_server_remove(srv);
        } else {
            lsp_close_fds(srv);
            srv->initialized = 0;
        }
        ed_set_status_message("LSP[%s]: disconnected", lang_copy);
        return;
    }
    jrpc_reader_feed(&srv->reader, tmp, (size_t)n);

    /* Parse and dispatch complete messages */
    char *body;
    size_t blen;
    while ((body = jrpc_reader_next(&srv->reader, &blen))) {
        lsp_handle_message(srv, body, (int)blen);
        free(body);
    }
}

/* ---------------------------------------------------- buffer notifications */

/* Record the just-synced state of `buf` so lsp_sync_document can skip
 * redundant didChange notifications. */
static void lsp_sync_mark(Buffer *buf) {
    if (!buf || !buf->filename)
        return;
    LspSyncEnt *e = lsp_sync_slot(buf->filename, 1);
    if (e) {
        e->gen = undo_mod_generation();
        e->dirty = buf->dirty;
    }
}

/* LSP is opt-in: nothing is spawned until :lsp_start (or :lsp_connect),
 * unless the user turns auto-start on (:lsp_autostart / config). */
static int g_autostart = 0;

void lsp_set_autostart(int on) { g_autostart = on ? 1 : 0; }
int lsp_get_autostart(void) { return g_autostart; }

void lsp_on_buffer_open(Buffer *buf) {
    if (!buf || !buf->filename || !buf->filetype)
        return;
    LspServer *srv = lsp_server_for_buffer(buf);

    /* Auto-start (opt-in): if no server is running for this filetype
     * but the registry has an entry, spawn it. The initialize-response
     * handler will replay didOpen for this buffer once the handshake
     * completes, so we just return here. */
    if (!srv && g_autostart && lsp_servers_lookup(buf->filetype)) {
        log_msg("LSP: auto-start triggered for %s (file=%s)", buf->filetype,
                buf->filename);
        lsp_cmd_start(buf->filetype, buf->filename);
        return;
    }
    if (!srv || !srv->initialized)
        return;

    char *uri = lsp_get_file_uri(buf->filename);
    char *content = buf_to_text(buf, NULL);
    if (!uri || !content) {
        free(uri);
        free(content);
        return;
    }

    cJSON *params = cJSON_CreateObject();
    cJSON *textdoc = cJSON_CreateObject();
    cJSON_AddStringToObject(textdoc, "uri", uri);
    cJSON_AddStringToObject(textdoc, "languageId", buf->filetype);
    cJSON_AddNumberToObject(textdoc, "version", g_doc_version++);
    cJSON_AddStringToObject(textdoc, "text", content);
    cJSON_AddItemToObject(params, "textDocument", textdoc);

    lsp_send_notification(srv, "textDocument/didOpen", params);
    lsp_sync_mark(buf);
    free(uri);
    free(content);
}

void lsp_on_buffer_close(Buffer *buf) {
    if (!buf || !buf->filename)
        return;
    lsp_sync_forget(buf->filename);
    LspServer *srv = lsp_server_for_buffer(buf);
    if (!srv || !srv->initialized)
        return;

    char *uri = lsp_get_file_uri(buf->filename);
    if (!uri)
        return;

    cJSON *params = cJSON_CreateObject();
    cJSON *textdoc = cJSON_CreateObject();
    cJSON_AddStringToObject(textdoc, "uri", uri);
    cJSON_AddItemToObject(params, "textDocument", textdoc);
    lsp_send_notification(srv, "textDocument/didClose", params);
    free(uri);
}

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
    cJSON *textdoc = cJSON_CreateObject();
    cJSON_AddStringToObject(textdoc, "uri", uri);
    cJSON_AddItemToObject(params, "textDocument", textdoc);
    lsp_send_notification(srv, "textDocument/didSave", params);
    free(uri);
}

/* Full-document didChange, sent unconditionally. Prefer
 * lsp_sync_document, which skips when the buffer hasn't changed. */
void lsp_on_buffer_changed(Buffer *buf) {
    if (!buf || !buf->filename || !buf->filetype)
        return;
    LspServer *srv = lsp_server_for_buffer(buf);
    if (!srv || !srv->initialized)
        return;

    char *uri = lsp_get_file_uri(buf->filename);
    char *content = buf_to_text(buf, NULL);
    if (!uri || !content) {
        free(uri);
        free(content);
        return;
    }

    cJSON *params = cJSON_CreateObject();
    cJSON *textdoc = cJSON_CreateObject();
    cJSON_AddStringToObject(textdoc, "uri", uri);
    cJSON_AddNumberToObject(textdoc, "version", g_doc_version++);
    cJSON_AddItemToObject(params, "textDocument", textdoc);

    /* Full-document sync: contentChanges is a single entry with the whole text
     */
    cJSON *changes = cJSON_CreateArray();
    cJSON *change = cJSON_CreateObject();
    cJSON_AddStringToObject(change, "text", content);
    cJSON_AddItemToArray(changes, change);
    cJSON_AddItemToObject(params, "contentChanges", changes);

    lsp_send_notification(srv, "textDocument/didChange", params);
    lsp_sync_mark(buf);
    free(uri);
    free(content);
}

/* Send a didChange only when the buffer moved since the last sync.
 * Called before every request so the server always sees the live text
 * (typing in insert mode doesn't push per-keystroke syncs). */
void lsp_sync_document(Buffer *buf) {
    if (!buf || !buf->filename)
        return;
    LspSyncEnt *e = lsp_sync_slot(buf->filename, 0);
    if (e && e->gen == undo_mod_generation() && e->dirty == buf->dirty)
        return;
    lsp_on_buffer_changed(buf);
}

/* ---------------------------------------------------- user-facing requests */

/* Resolve the ready server for `buf`, with status-line feedback. */
static LspServer *lsp_server_ready(Buffer *buf, int quiet) {
    if (!buf || !buf->filename)
        return NULL;
    LspServer *srv = lsp_server_for_buffer(buf);
    if (!srv) {
        if (!quiet)
            ed_set_status_message("LSP[%s]: no server (try :lsp_start)",
                                  buf->filetype ? buf->filetype : "?");
        return NULL;
    }
    if (!srv->initialized) {
        if (!quiet)
            ed_set_status_message("LSP[%s]: still initializing, try again",
                                  srv->lang);
        return NULL;
    }
    return srv;
}

/* TextDocumentPositionParams for (buf, line, byte col). Converts the
 * column to UTF-16 code units. Returns NULL on OOM/bad uri. */
static cJSON *lsp_textdoc_position(Buffer *buf, int line, int col) {
    char *uri = lsp_get_file_uri(buf->filename);
    if (!uri)
        return NULL;
    int u16 = col;
    if (line >= 0 && line < buf->num_rows) {
        Row *row = &buf->rows[line];
        u16 = lsp_cx_to_utf16(row->chars.data, row->chars.len, col);
    }
    cJSON *params = cJSON_CreateObject();
    cJSON *textdoc = cJSON_CreateObject();
    cJSON_AddStringToObject(textdoc, "uri", uri);
    cJSON_AddItemToObject(params, "textDocument", textdoc);
    cJSON *pos = cJSON_CreateObject();
    cJSON_AddNumberToObject(pos, "line", line);
    cJSON_AddNumberToObject(pos, "character", u16);
    cJSON_AddItemToObject(params, "position", pos);
    free(uri);
    return params;
}

void lsp_request_hover(Buffer *buf, int line, int col) {
    LspServer *srv = lsp_server_ready(buf, 0);
    if (!srv)
        return;
    lsp_sync_document(buf);
    cJSON *params = lsp_textdoc_position(buf, line, col);
    if (!params)
        return;
    int id = srv->next_id++;
    lsp_pending_add(srv, id, LSP_REQ_HOVER);
    lsp_send_request(srv, "textDocument/hover", params, id);
}

void lsp_request_definition(Buffer *buf, int line, int col) {
    LspServer *srv = lsp_server_ready(buf, 0);
    if (!srv)
        return;
    lsp_sync_document(buf);
    cJSON *params = lsp_textdoc_position(buf, line, col);
    if (!params)
        return;
    int id = srv->next_id++;
    lsp_pending_add(srv, id, LSP_REQ_DEFINITION);
    lsp_send_request(srv, "textDocument/definition", params, id);
}

/* :definition dispatcher probe (see plugins/ctags): issue an LSP
 * definition request for the current buffer if a ready server is
 * attached. Returns 0 when the request went out, -1 to fall back. */
int lsp_definition_try(void) {
    Buffer *buf = buf_cur();
    if (!buf)
        return -1;
    if (!lsp_server_ready(buf, 1))
        return -1;
    Window *win = window_cur();
    int line = win && !win->is_modal ? win->cursor.y : buf->cursor->y;
    int col = win && !win->is_modal ? win->cursor.x : buf->cursor->x;
    lsp_request_definition(buf, line, col);
    return 0;
}

/* ------------------------------------------------ completion source
 * The completion plugin owns the menu; this source feeds it. */

static int lsp_cmp_available(Buffer *buf) {
    return lsp_server_ready(buf, 1) != NULL;
}

static int lsp_cmp_is_trigger(Buffer *buf, int c) {
    LspServer *srv = lsp_server_for_buffer(buf);
    if (!srv || !srv->initialized || c <= 0 || c >= 128)
        return 0;
    return strchr(srv->trigger_chars, c) != NULL;
}

static void lsp_cmp_request(Buffer *buf, int line, int col, unsigned token) {
    LspServer *srv = lsp_server_ready(buf, 1);
    if (!srv) {
        completion_provide(token, NULL, 0);
        return;
    }
    lsp_sync_document(buf);
    cJSON *params = lsp_textdoc_position(buf, line, col);
    if (!params)
        return;
    int buf_idx = (int)(buf - E.buffers);
    int id = srv->next_id++;
    lsp_pending_add_completion(srv, id, buf_idx, line, col, token);
    lsp_send_request(srv, "textDocument/completion", params, id);
}

static const CompletionSource lsp_cmp_source = {
    .name = "lsp",
    .available = lsp_cmp_available,
    .is_trigger_char = lsp_cmp_is_trigger,
    .request = lsp_cmp_request,
};

void lsp_completion_source_register(void) {
    completion_source_register(&lsp_cmp_source);
}

/* ------------------------------------------------ connect / disconnect */

static LspServer *lsp_server_alloc(const char *lang, const char *root_uri) {
    if (g_servers_count >= LSP_MAX_SERVERS)
        return NULL;
    LspServer *srv = calloc(1, sizeof(LspServer));
    if (!srv)
        return NULL;
    srv->lang = strdup(lang);
    srv->root_uri = strdup(root_uri ? root_uri : "file:///");
    srv->to_fd = -1;
    srv->from_fd = -1;
    srv->initialized = 0;
    srv->initialize_id = -1;
    srv->next_id = 1;
    jrpc_reader_init(&srv->reader);
    for (int i = 0; i < LSP_MAX_SERVERS; i++) {
        if (!g_servers[i]) {
            g_servers[i] = srv;
            g_servers_count++;
            break;
        }
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
static int lsp_connect_pipe(LspServer *srv, const char *to_path,
                            const char *from_path) {
    srv->to_fd = open(to_path, O_RDWR);
    if (srv->to_fd < 0) {
        log_msg("LSP: open to_pipe %s: %s", to_path, strerror(errno));
        return -1;
    }
    srv->from_fd = open(from_path, O_RDWR);
    if (srv->from_fd < 0) {
        log_msg("LSP: open from_pipe %s: %s", from_path, strerror(errno));
        close(srv->to_fd);
        srv->to_fd = -1;
        return -1;
    }
    int fl = fcntl(srv->from_fd, F_GETFL, 0);
    fcntl(srv->from_fd, F_SETFL, fl | O_NONBLOCK);
    log_msg("LSP[%s]: connected via pipes %s / %s", srv->lang, to_path,
            from_path);
    return 0;
}

/* Connect via TCP socket to host:port. */
static int lsp_connect_tcp(LspServer *srv, const char *host, int port) {
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
        log_msg("LSP: getaddrinfo %s:%d failed: %s", host, port,
                strerror(errno));
        return -1;
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(res);
        return -1;
    }

    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        log_msg("LSP: connect %s:%d: %s", host, port, strerror(errno));
        close(sock);
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);

    int fl = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, fl | O_NONBLOCK);

    /* TCP is bidirectional; use the same fd for both directions. */
    srv->to_fd = sock;
    srv->from_fd = sock;
    log_msg("LSP[%s]: connected via TCP %s:%d", srv->lang, host, port);
    return 0;
}

/* Spawn the server as a child process, wiring its stdio to two pipes. */
static int lsp_spawn_process(LspServer *srv, const char *const *argv) {
    Proc pr;
    if (proc_spawn(argv, PROC_STDIN, &pr) != 0) {
        log_msg("LSP[%s]: spawn '%s' failed — binary missing from $PATH? "
                "see the editor log",
                srv->lang, argv[0]);
        return -1;
    }
    srv->pid = pr.pid;
    srv->to_fd = pr.to_fd;
    srv->from_fd = pr.from_fd;
    return 0;
}

/* Walk up from `start` looking for any of `markers`. Returns 0 and writes
 * the absolute path of the first directory that contains a marker into
 * `out` (size `out_sz`); returns -1 if none found. */
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
    if (!fs_find_root_marker(hint_path && *hint_path ? hint_path : E.cwd,
                             def->root_markers, root_dir, sizeof(root_dir))) {
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
    if (!srv) {
        ed_set_status_message("LSP: too many servers");
        return -1;
    }

    if (lsp_spawn_process(srv, def->argv) < 0) {
        ed_set_status_message("LSP[%s]: spawn '%s' failed (is it installed?)",
                              lang, def->argv[0]);
        lsp_server_remove(srv);
        return -1;
    }

    ed_loop_register(srv->lang, srv->from_fd, lsp_on_readable, srv);
    lsp_send_initialize(srv);
    ed_set_status_message("LSP[%s]: starting %s (root %s)", lang, def->argv[0],
                          root_dir);
    return 0;
}

int lsp_cmd_connect(const char *lang, const char *to_addr,
                    const char *from_addr, const char *root_uri) {
    if (!lang || !to_addr) {
        ed_set_status_message(
            "LSP: usage: lsp_connect <lang> tcp <host>:<port>  "
            "or  lsp_connect <lang> <to_pipe> <from_pipe>");
        return -1;
    }

    if (lsp_server_for_lang(lang)) {
        ed_set_status_message(
            "LSP[%s]: already connected (use lsp_disconnect first)", lang);
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
        int port = 0;
        /* parse last colon as separator so IPv6 addrs work */
        const char *colon = strrchr(from_addr, ':');
        if (!colon) {
            ed_set_status_message(
                "LSP: invalid address '%s' (expected host:port)", from_addr);
            goto fail;
        }
        size_t hlen = (size_t)(colon - from_addr);
        if (hlen >= sizeof(host))
            hlen = sizeof(host) - 1;
        memcpy(host, from_addr, hlen);
        host[hlen] = '\0';
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
    lsp_server_remove(srv);
    return -1;
}

int lsp_cmd_disconnect(const char *lang) {
    if (!lang) {
        ed_set_status_message("LSP: specify a language");
        return -1;
    }
    LspServer *srv = lsp_server_for_lang(lang);
    if (!srv) {
        ed_set_status_message("LSP[%s]: not connected", lang);
        return -1;
    }

    char lang_copy[64];
    snprintf(lang_copy, sizeof(lang_copy), "%s", srv->lang);
    lsp_server_disconnect_polite(srv);
    ed_set_status_message("LSP[%s]: disconnected", lang_copy);
    return 0;
}

void lsp_cmd_status(void) {
    if (g_servers_count == 0) {
        ed_set_status_message("LSP: no servers connected");
        return;
    }
    char buf[512];
    int off = 0;
    off += snprintf(buf + off, sizeof(buf) - (size_t)off, "LSP:");
    for (int i = 0; i < LSP_MAX_SERVERS; i++) {
        LspServer *srv = g_servers[i];
        if (!srv)
            continue;
        off +=
            snprintf(buf + off, sizeof(buf) - (size_t)off, " [%s %s fd=%d/%d]",
                     srv->lang, srv->initialized ? "ready" : "init", srv->to_fd,
                     srv->from_fd);
    }
    ed_set_status_message("%s", buf);
}
