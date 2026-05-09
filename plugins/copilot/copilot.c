/* copilot plugin — GitHub Copilot ghost-text completion via the
 * @github/copilot-language-server JSON-RPC server.
 *
 * Flow:
 *   :copilot login              -> signInInitiate, prints userCode + URL
 *   :copilot login <userCode>   -> signInConfirm
 *   :copilot status             -> checkStatus
 *   :copilot logout             -> signOut
 *   :copilot enable / disable   -> turn on/off ghost text on edit
 *
 * Insert mode + at EOL: every char insert schedules a debounced
 * getCompletions; the first returned completion is rendered as EOL
 * virtual text. Tab inserts it. Cursor move / mode change dismisses it. */

#include "hed.h"
#include "select_loop.h"
#include "buf/virtual_text.h"
#include "copilot.h"
#include "copilot_internal.h"
#include "lsp/cjson/cJSON.h"
#include "lsp/json_helpers.h"

/* Internal helper that lsp_impl.c also forward-declares.
 * Declared in buf/buffer.c but not exposed in buffer.h. */
void buf_row_insert_in(Buffer *buf, int at, const char *s, size_t len);

/* ----- module-level config ----- */

static int  g_enabled   = 1;   /* default on once spawned + signed in */

/* ----- helpers ----- */

static char *cp_uri_for(const char *filepath) {
    if (!filepath) return NULL;
    char  cwd[1024] = {0};
    char *uri = malloc(strlen(filepath) + strlen(E.cwd) + 32);
    if (!uri) return NULL;
    if (filepath[0] == '/') {
        sprintf(uri, "file://%s", filepath);
    } else if (E.cwd[0]) {
        sprintf(uri, "file://%s/%s", E.cwd, filepath);
    } else if (getcwd(cwd, sizeof(cwd))) {
        sprintf(uri, "file://%s/%s", cwd, filepath);
    } else {
        sprintf(uri, "file://%s", filepath);
    }
    return uri;
}

static char *cp_buffer_text(Buffer *buf) {
    size_t total = 0;
    for (int i = 0; i < buf->num_rows; i++)
        total += buf->rows[i].chars.len + 1;
    char *s = malloc(total + 1);
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

static const char *cp_language_id(Buffer *buf) {
    /* Copilot accepts VSCode languageIds. The editor's filetype strings
     * (c, python, rust, javascript, typescript, go, ...) match closely
     * enough for common languages. Empty/unknown -> "plaintext". */
    if (!buf || !buf->filetype || !*buf->filetype) return "plaintext";
    return buf->filetype;
}

static int cp_cursor_at_eol(Buffer *buf) {
    if (!buf || !buf->cursor) return 0;
    int y = buf->cursor->y;
    if (y < 0 || y >= buf->num_rows) return 0;
    return buf->cursor->x == (int)buf->rows[y].chars.len;
}

/* ----- pane (alternatives buffer) ----- */

#define COPILOT_PANE_TITLE "[copilot]"

static int cp_pane_buf_idx(void) {
    for (int i = 0; i < (int)arrlen(E.buffers); i++) {
        const char *t = E.buffers[i].title;
        if (t && strcmp(t, COPILOT_PANE_TITLE) == 0) return i;
    }
    return -1;
}

static int cp_is_pane_buf(const Buffer *b) {
    return b && b->title && strcmp(b->title, COPILOT_PANE_TITLE) == 0;
}

static int cp_pane_window_idx(int buf_idx) {
    for (int i = 0; i < (int)arrlen(E.windows); i++) {
        Window *w = &E.windows[i];
        if (w->visible && !w->is_modal && w->buffer_index == buf_idx) return i;
    }
    return -1;
}

static int cp_pane_get_or_create_buf(void) {
    int idx = cp_pane_buf_idx();
    if (idx >= 0) return idx;
    int new_idx = -1;
    if (buf_new(NULL, &new_idx) != ED_OK) return -1;
    Buffer *b = &E.buffers[new_idx];
    free(b->filename); b->filename = NULL;
    free(b->title);    b->title    = strdup(COPILOT_PANE_TITLE);
    b->dirty = 0;
    b->readonly = 1;
    return new_idx;
}

static void cp_pane_refresh(void) {
    int idx = cp_pane_buf_idx();
    if (idx < 0) return;          /* pane not open — nothing to draw */
    Buffer *b = &E.buffers[idx];

    /* Clear existing rows. */
    for (int i = 0; i < b->num_rows; i++) row_free(&b->rows[i]);
    free(b->rows);
    b->rows     = NULL;
    b->num_rows = 0;
    if (b->cursor) { b->cursor->x = 0; b->cursor->y = 0; }

    if (CP.alts_count == 0) {
        const char *msg = "(no suggestions)";
        buf_row_insert_in(b, 0, msg, strlen(msg));
        b->dirty = 0;
        return;
    }

    /* Each alt -> one or more rows: "[*] N: <first line>\n   <rest>". */
    for (int i = 0; i < CP.alts_count; i++) {
        const char *disp = CP.alts[i].display ? CP.alts[i].display
                                              : CP.alts[i].text;
        if (!disp) disp = "";
        const char *p = disp;
        int line_no = 0;
        while (1) {
            const char *nl = strchr(p, '\n');
            int seg = nl ? (int)(nl - p) : (int)strlen(p);
            char hdr[16];
            if (line_no == 0) {
                snprintf(hdr, sizeof(hdr), "%s%d: ",
                         i == CP.alts_active ? "* " : "  ", i + 1);
            } else {
                snprintf(hdr, sizeof(hdr), "     ");
            }
            int hlen = (int)strlen(hdr);
            int rowlen = hlen + seg;
            char *row = malloc((size_t)rowlen + 1);
            if (row) {
                memcpy(row, hdr, (size_t)hlen);
                memcpy(row + hlen, p, (size_t)seg);
                row[rowlen] = '\0';
                buf_row_insert_in(b, b->num_rows, row, (size_t)rowlen);
                free(row);
            }
            line_no++;
            if (!nl) break;
            p = nl + 1;
        }
    }
    b->dirty = 0;
}

/* ----- ghost text rendering / clearing ----- */

static void cp_alts_free(void) {
    for (int i = 0; i < CP.alts_count; i++) {
        free(CP.alts[i].text);
        free(CP.alts[i].display);
        free(CP.alts[i].uuid);
    }
    free(CP.alts);
    CP.alts        = NULL;
    CP.alts_count  = 0;
    CP.alts_active = 0;
}

static void cp_clear_suggestion(void) {
    if (!CP.has_suggestion && CP.alts_count == 0) return;
    for (ptrdiff_t i = 0; i < arrlen(E.buffers); i++) {
        vtext_clear_ns(&E.buffers[i], CP.vt_ns);
    }
    free(CP.suggestion_text);    CP.suggestion_text    = NULL;
    free(CP.suggestion_display); CP.suggestion_display = NULL;
    free(CP.suggestion_uuid);    CP.suggestion_uuid    = NULL;
    CP.has_suggestion = 0;
    cp_alts_free();
    cp_pane_refresh();
}

/* Notify the server that the user dismissed without accepting. */
static void cp_notify_rejected(const char *uuid) {
    if (!uuid) return;
    cJSON *p = cJSON_CreateObject();
    cJSON *a = cJSON_CreateArray();
    cJSON_AddItemToArray(a, cJSON_CreateString(uuid));
    cJSON_AddItemToObject(p, "uuids", a);
    cp_proto_notify("notifyRejected", p);
}

static void cp_dismiss(void) {
    if (CP.suggestion_uuid) cp_notify_rejected(CP.suggestion_uuid);
    cp_clear_suggestion();
}

/* ----- completion request ----- */

/* Full-text sync: tell the server the current contents of `buf` and
 * bump `CP.doc_version`. The matching getCompletions call below
 * reuses (does not bump) the same version. */
static void cp_send_did_change(Buffer *buf, const char *uri, const char *text) {
    cJSON *p   = cJSON_CreateObject();
    cJSON *td  = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri",     uri);
    cJSON_AddNumberToObject(td, "version", ++CP.doc_version);
    cJSON_AddItemToObject(p, "textDocument", td);

    cJSON *changes = cJSON_CreateArray();
    cJSON *change  = cJSON_CreateObject();
    cJSON_AddStringToObject(change, "text", text);   /* full document */
    cJSON_AddItemToArray(changes, change);
    cJSON_AddItemToObject(p, "contentChanges", changes);

    cp_proto_notify("textDocument/didChange", p);
    (void)buf;
}

static void cp_request_completions_for_cursor(Buffer *buf) {
    if (!CP.spawned || !CP.initialized || !CP.signed_in) return;
    if (!buf || !buf->filename || !buf->cursor) return;
    if (cp_is_pane_buf(buf)) return;        /* never request for our own pane */

    char *uri  = cp_uri_for(buf->filename);
    char *text = cp_buffer_text(buf);
    if (!uri || !text) { free(uri); free(text); return; }

    /* Sync the document FIRST so the server's view matches what we're
     * about to point at with `position`. The version we set here is
     * the one we cite in getCompletions immediately below. */
    cp_send_did_change(buf, uri, text);

    cJSON *params = cJSON_CreateObject();
    cJSON *doc    = cJSON_CreateObject();
    cJSON_AddStringToObject(doc, "uri",          uri);
    cJSON_AddNumberToObject(doc, "version",      CP.doc_version);
    cJSON_AddStringToObject(doc, "languageId",   cp_language_id(buf));
    cJSON_AddStringToObject(doc, "relativePath", buf->filename);
    cJSON_AddBoolToObject  (doc, "insertSpaces", E.expand_tab ? 1 : 0);
    cJSON_AddNumberToObject(doc, "tabSize",      E.tab_size > 0 ? E.tab_size : 4);
    cJSON_AddNumberToObject(doc, "indentSize",   E.tab_size > 0 ? E.tab_size : 4);

    cJSON *pos = cJSON_CreateObject();
    cJSON_AddNumberToObject(pos, "line",      buf->cursor->y);
    /* Byte column. Strict UTF-16 code-unit math is a v2 concern;
     * ASCII files are the common case. */
    cJSON_AddNumberToObject(pos, "character", buf->cursor->x);
    cJSON_AddItemToObject(doc, "position", pos);

    cJSON_AddItemToObject(params, "doc", doc);

    cp_proto_request("getCompletions", params, CP_REQ_GET_COMPLETIONS);
    free(uri); free(text);
}

/* ----- debounce ----- */

static void cp_debounced_fire(void *ud) {
    (void)ud;
    Buffer *buf = buf_cur();
    if (!buf) return;
    if (E.mode != MODE_INSERT) return;
    if (!cp_cursor_at_eol(buf)) return;     /* v1: EOL only */
    cp_request_completions_for_cursor(buf);
}

static void cp_schedule(void) {
    if (!g_enabled || !CP.spawned || !CP.initialized || !CP.signed_in) return;
    ed_loop_timer_after("copilot:debounce", CP_DEBOUNCE_MS,
                        cp_debounced_fire, NULL);
}

/* ----- hooks ----- */

static void on_char_insert(const HookCharEvent *e) {
    (void)e;
    cp_clear_suggestion();
    cp_schedule();
}

static void on_char_delete(const HookCharEvent *e) {
    (void)e;
    cp_clear_suggestion();
    cp_schedule();
}

static void on_cursor_move(const HookCursorEvent *e) {
    if (!CP.has_suggestion) return;
    /* If the cursor moved off the suggestion's anchor, dismiss. */
    if (e && (e->new_y != CP.suggestion_line || e->new_x != CP.suggestion_col))
        cp_dismiss();
}

static void on_mode_change(const HookModeEvent *e) {
    if (!e) return;
    if (e->new_mode == MODE_INSERT) {
        /* Trigger a fetch on mode entry so empty rows (and rows the
         * user opened with `o`/`O`/`a`/`i` without typing anything
         * yet) still get a suggestion. */
        cp_schedule();
    } else {
        ed_loop_timer_cancel("copilot:debounce");
        cp_dismiss();
    }
}

static void cp_send_did_open(Buffer *buf) {
    if (!buf || !buf->filename || !CP.spawned || !CP.initialized) return;
    if (cp_is_pane_buf(buf)) return;        /* never sync our own pane */
    char *uri  = cp_uri_for(buf->filename);
    char *text = cp_buffer_text(buf);
    if (!uri || !text) { free(uri); free(text); return; }

    cJSON *p   = cJSON_CreateObject();
    cJSON *td  = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri",        uri);
    cJSON_AddStringToObject(td, "languageId", cp_language_id(buf));
    cJSON_AddNumberToObject(td, "version",    ++CP.doc_version);
    cJSON_AddStringToObject(td, "text",       text);
    cJSON_AddItemToObject(p, "textDocument", td);
    cp_proto_notify("textDocument/didOpen", p);
    free(uri); free(text);
}

static void cp_notify_existing_buffers(void) {
    for (ptrdiff_t i = 0; i < arrlen(E.buffers); i++) {
        cp_send_did_open(&E.buffers[i]);
    }
}

static void on_buffer_open(HookBufferEvent *e) {
    if (!e || !e->buf) return;
    cp_send_did_open(e->buf);
}

static void on_buffer_close(HookBufferEvent *e) {
    if (!e || !e->buf || !CP.spawned || !CP.initialized) return;
    if (!e->buf->filename) return;
    if (cp_is_pane_buf(e->buf)) return;
    char *uri = cp_uri_for(e->buf->filename);
    if (!uri) return;
    cJSON *p  = cJSON_CreateObject();
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", uri);
    cJSON_AddItemToObject(p, "textDocument", td);
    cp_proto_notify("textDocument/didClose", p);
    free(uri);
}

/* ----- handshake ----- */

static void cp_send_initialize(void) {
    cJSON *params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "processId", (double)getpid());
    cJSON_AddNullToObject(params, "rootUri");
    if (E.cwd[0]) {
        char uri[PATH_MAX + 16];
        snprintf(uri, sizeof(uri), "file://%s", E.cwd);
        cJSON_DeleteItemFromObject(params, "rootUri");
        cJSON_AddStringToObject(params, "rootUri", uri);
    }

    cJSON *caps = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "capabilities", caps);

    cJSON *opts = cJSON_CreateObject();
    cJSON *ed   = cJSON_CreateObject();
    cJSON_AddStringToObject(ed, "name",    "hed");
    cJSON_AddStringToObject(ed, "version", HED_VERSION);
    cJSON_AddItemToObject(opts, "editorInfo", ed);
    cJSON *pl   = cJSON_CreateObject();
    cJSON_AddStringToObject(pl, "name",    "hed-copilot");
    cJSON_AddStringToObject(pl, "version", "0.1");
    cJSON_AddItemToObject(opts, "editorPluginInfo", pl);
    cJSON_AddItemToObject(params, "initializationOptions", opts);

    cp_proto_request("initialize", params, CP_REQ_INITIALIZE);
}

static void cp_send_initialized(void) {
    cp_proto_notify("initialized", cJSON_CreateObject());
    /* Tell the server about our telemetry stance so it stops nagging. */
    cJSON *cfg = cJSON_CreateObject();
    cJSON *settings = cJSON_CreateObject();
    cJSON *tele = cJSON_CreateObject();
    cJSON_AddStringToObject(tele, "telemetryLevel", "off");
    cJSON_AddItemToObject(settings, "telemetry", tele);
    cJSON_AddItemToObject(cfg, "settings", settings);
    cp_proto_notify("workspace/didChangeConfiguration", cfg);

    CP.initialized = 1;
    log_msg("copilot: initialized");
    /* Ask the server whether we already have a usable token cached. */
    cp_proto_request("checkStatus", cJSON_CreateObject(), CP_REQ_CHECK_STATUS);
}

/* ----- response handlers ----- */

/* Render the currently-active alternative as ghost text on the focused
 * buffer at the supplied anchor. Frees and replaces any prior ghost. */
static void cp_show_active(int line, int col) {
    /* Clear prior ghost-text marks (but keep alts[]). */
    for (ptrdiff_t i = 0; i < arrlen(E.buffers); i++) {
        vtext_clear_ns(&E.buffers[i], CP.vt_ns);
    }
    free(CP.suggestion_text);    CP.suggestion_text    = NULL;
    free(CP.suggestion_display); CP.suggestion_display = NULL;
    free(CP.suggestion_uuid);    CP.suggestion_uuid    = NULL;
    CP.has_suggestion = 0;

    if (CP.alts_count == 0) return;
    if (CP.alts_active < 0 || CP.alts_active >= CP.alts_count)
        CP.alts_active = 0;

    const char *text = CP.alts[CP.alts_active].text;
    const char *disp = CP.alts[CP.alts_active].display;
    const char *uuid = CP.alts[CP.alts_active].uuid;
    if (!disp) disp = text;
    if (!disp || !*disp) return;

    Buffer *buf = buf_cur();
    if (!buf || !buf->cursor) return;
    if (line < 0 || col < 0) { line = buf->cursor->y; col = buf->cursor->x; }

    /* First line of the suggestion goes on the anchor row as EOL
     * ghost text. Any tail lines get one BLOCK_BELOW mark so they
     * render as virtual rows directly under the anchor. */
    const char *nl = strchr(disp, '\n');
    int show_len   = nl ? (int)(nl - disp) : (int)strlen(disp);
    if (show_len > 0)
        vtext_set_eol(buf, CP.vt_ns, line, disp, (size_t)show_len, NULL);

    if (nl && *(nl + 1)) {
        const char *tail = nl + 1;
        /* Strip a single trailing newline so we don't render an empty
         * virtual line at the bottom of the block. */
        size_t tail_len = strlen(tail);
        while (tail_len > 0 && tail[tail_len - 1] == '\n') tail_len--;
        if (tail_len > 0)
            vtext_set_block_below(buf, CP.vt_ns, line,
                                  tail, tail_len, NULL);
    }
    if (show_len <= 0 && !(nl && *(nl + 1))) return;

    CP.suggestion_text    = text ? strdup(text) : strdup(disp);
    CP.suggestion_display = strdup(disp);
    CP.suggestion_uuid    = uuid ? strdup(uuid) : NULL;
    CP.suggestion_line    = line;
    CP.suggestion_col     = col;
    CP.has_suggestion     = 1;

    if (CP.suggestion_uuid) {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "uuid", CP.suggestion_uuid);
        cp_proto_notify("notifyShown", p);
    }
}

static void cp_apply_suggestion(cJSON *result) {
    /* Drop prior ghost + alt list (this is a fresh response). */
    cp_clear_suggestion();
    if (!result) return;

    cJSON *arr = cJSON_GetObjectItemCaseSensitive(result, "completions");
    if (!arr || !cJSON_IsArray(arr)) {
        cp_pane_refresh();
        return;
    }
    int n = cJSON_GetArraySize(arr);
    if (n <= 0) {
        log_msg("copilot: no completions");
        cp_pane_refresh();
        return;
    }

    /* Read anchor from the first completion (server's reference point). */
    cJSON *first  = cJSON_GetArrayItem(arr, 0);
    cJSON *posobj = cJSON_GetObjectItemCaseSensitive(first, "position");
    int line = posobj ? json_get_int(posobj, "line", -1) : -1;
    int col  = posobj ? json_get_int(posobj, "character", -1) : -1;

    CP.alts        = calloc((size_t)n, sizeof(*CP.alts));
    CP.alts_count  = 0;
    CP.alts_active = 0;
    if (!CP.alts) return;
    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        const char *t = json_get_string(item, "text");
        const char *d = json_get_string(item, "displayText");
        const char *u = json_get_string(item, "uuid");
        CP.alts[CP.alts_count].text    = t ? strdup(t) : NULL;
        CP.alts[CP.alts_count].display = d ? strdup(d) : (t ? strdup(t) : NULL);
        CP.alts[CP.alts_count].uuid    = u ? strdup(u) : NULL;
        CP.alts_count++;
    }

    cp_show_active(line, col);
    cp_pane_refresh();
}

static void cp_handle_check_status(cJSON *result) {
    const char *status = result ? json_get_string(result, "status") : NULL;
    const char *user   = result ? json_get_string(result, "user")   : NULL;
    if (status && strcmp(status, "OK") == 0) {
        int was_signed_in = CP.signed_in;
        CP.signed_in = 1;
        if (user) {
            strncpy(CP.user_login, user, sizeof(CP.user_login) - 1);
            CP.user_login[sizeof(CP.user_login) - 1] = '\0';
        }
        ed_set_status_message("copilot: signed in as %s",
                              user ? user : "(unknown)");
        /* First time we know the session is usable: catch the server up
         * on every buffer the editor already had open before we
         * connected. */
        if (!was_signed_in) cp_notify_existing_buffers();
    } else {
        CP.signed_in = 0;
        ed_set_status_message("copilot: %s - run :copilot login",
                              status ? status : "not signed in");
    }
}

static void cp_handle_sign_in_initiate(cJSON *result) {
    const char *code = result ? json_get_string(result, "userCode")        : NULL;
    const char *uri  = result ? json_get_string(result, "verificationUri") : NULL;
    if (!code || !uri) {
        ed_set_status_message("copilot: signInInitiate returned no userCode");
        return;
    }
    strncpy(CP.user_code, code, sizeof(CP.user_code) - 1);
    CP.user_code[sizeof(CP.user_code) - 1] = '\0';
    ed_set_status_message(
        "copilot: open %s and enter code %s, then :copilot login %s",
        uri, code, code);
    log_msg("copilot: device code=%s url=%s", code, uri);
}

static void cp_handle_sign_in_confirm(cJSON *result) {
    const char *status = result ? json_get_string(result, "status") : NULL;
    const char *user   = result ? json_get_string(result, "user")   : NULL;
    if (status && strcmp(status, "OK") == 0) {
        int was_signed_in = CP.signed_in;
        CP.signed_in = 1;
        if (user) {
            strncpy(CP.user_login, user, sizeof(CP.user_login) - 1);
            CP.user_login[sizeof(CP.user_login) - 1] = '\0';
        }
        ed_set_status_message("copilot: signed in as %s",
                              user ? user : "(unknown)");
        if (!was_signed_in) cp_notify_existing_buffers();
    } else {
        ed_set_status_message("copilot: sign-in failed (%s)",
                              status ? status : "unknown");
    }
}

static void cp_process_response(cJSON *json) {
    int id = json_get_int(json, "id", -1);
    cJSON *err = cJSON_GetObjectItemCaseSensitive(json, "error");
    if (err) {
        const char *msg = json_get_string(err, "message");
        log_msg("copilot: error id=%d: %s", id, msg ? msg : "?");
        ed_set_status_message("copilot error: %s", msg ? msg : "(unknown)");
        cp_proto_pending_pop(id);
        return;
    }
    cJSON    *result = cJSON_GetObjectItemCaseSensitive(json, "result");
    CpReqKind kind   = cp_proto_pending_pop(id);

    switch (kind) {
    case CP_REQ_INITIALIZE:
        cp_send_initialized();
        break;
    case CP_REQ_CHECK_STATUS:
        cp_handle_check_status(result);
        break;
    case CP_REQ_SIGN_IN_INITIATE:
        cp_handle_sign_in_initiate(result);
        break;
    case CP_REQ_SIGN_IN_CONFIRM:
        cp_handle_sign_in_confirm(result);
        break;
    case CP_REQ_SIGN_OUT:
        CP.signed_in = 0;
        CP.user_login[0] = '\0';
        ed_set_status_message("copilot: signed out");
        break;
    case CP_REQ_GET_COMPLETIONS:
        cp_apply_suggestion(result);
        break;
    default:
        log_msg("copilot: untracked response id=%d", id);
        break;
    }
}

static void cp_process_notification(cJSON *json) {
    const char *method = json_get_string(json, "method");
    if (!method) return;
    log_msg("copilot: <- %s", method);
    if (strcmp(method, "window/logMessage") == 0 ||
        strcmp(method, "window/showMessage") == 0) {
        cJSON      *params = json_get_object(json, "params");
        const char *msg    = params ? json_get_string(params, "message") : NULL;
        if (msg) log_msg("copilot[srv]: %s", msg);
    }
}

void cp_handle_message(const char *json_str, int len) {
    (void)len;
    cJSON *json = json_parse(json_str, (size_t)len);
    if (!json) { log_msg("copilot: JSON parse error"); return; }
    cJSON *id = cJSON_GetObjectItemCaseSensitive(json, "id");
    if (id && !cJSON_IsNull(id)) cp_process_response(json);
    else                          cp_process_notification(json);
    cJSON_Delete(json);
}

/* ----- Tab to accept ----- */

static void kb_accept(void) {
    Buffer *buf = buf_cur();
    if (!CP.has_suggestion || !CP.suggestion_text || !buf || !buf->cursor) {
        /* Fall through to a literal tab. Using existing primitive that
         * respects E.expand_tab. */
        if (E.expand_tab) {
            int n = E.tab_size > 0 ? E.tab_size : 4;
            for (int i = 0; i < n; i++) buf_insert_char_in(buf, ' ');
        } else {
            buf_insert_char_in(buf, '\t');
        }
        return;
    }

    /* Suggestion accepted. Compute what's actually new (the suggestion
     * may include a prefix that overlaps text already typed before the
     * cursor on this row). */
    const char *full = CP.suggestion_text;
    int already_have = buf->cursor->x;     /* bytes typed on this row */
    /* Strip an exact-match prefix of length min(already_have, suggestion).
     * If the row's leading bytes don't match, fall back to inserting the
     * whole suggestion verbatim. */
    int row = buf->cursor->y;
    int strip = 0;
    if (row >= 0 && row < buf->num_rows) {
        const char *line = buf->rows[row].chars.data;
        int max = already_have;
        int flen = (int)strlen(full);
        if (max > flen) max = flen;
        while (strip < max && line[strip] == full[strip]) strip++;
        if (strip < max) strip = 0;   /* prefixes diverged — insert all */
    }

    /* cp_clear_suggestion() will free CP.suggestion_text, so make a
     * private copy of the bytes we're about to type before clearing —
     * otherwise the loop below reads freed memory and inserts garbage. */
    char *to_insert = strdup(full + strip);
    if (!to_insert) return;
    char *uuid = CP.suggestion_uuid;
    CP.suggestion_uuid = NULL;
    cp_clear_suggestion();

    for (const char *p = to_insert; *p; p++) {
        if (*p == '\n') buf_insert_newline_in(buf);
        else            buf_insert_char_in(buf, (unsigned char)*p);
    }

    /* notifyAccepted with the original uuid. */
    if (uuid) {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "uuid", uuid);
        cJSON_AddNumberToObject(p, "acceptedLength", (double)strlen(to_insert));
        cp_proto_notify("notifyAccepted", p);
        free(uuid);
    }
    free(to_insert);
}

/* ----- commands ----- */

static void cmd_copilot(const char *args) {
    while (args && *args == ' ') args++;
    if (!args || !*args || strcmp(args, "status") == 0) {
        if (!CP.spawned)        { ed_set_status_message("copilot: not running");        return; }
        if (!CP.initialized)    { ed_set_status_message("copilot: initializing...");    return; }
        if (!CP.signed_in)      { ed_set_status_message("copilot: not signed in");      return; }
        ed_set_status_message("copilot: signed in as %s, %s",
                              CP.user_login,
                              g_enabled ? "enabled" : "disabled");
        cp_proto_request("checkStatus", cJSON_CreateObject(), CP_REQ_CHECK_STATUS);
        return;
    }
    if (strcmp(args, "enable") == 0)  { g_enabled = 1; ed_set_status_message("copilot: enabled");  return; }
    if (strcmp(args, "disable") == 0) { g_enabled = 0; cp_dismiss(); ed_set_status_message("copilot: disabled"); return; }
    if (strcmp(args, "logout") == 0) {
        cp_proto_request("signOut", cJSON_CreateObject(), CP_REQ_SIGN_OUT);
        return;
    }
    if (strncmp(args, "login", 5) == 0 &&
        (args[5] == '\0' || args[5] == ' ')) {
        const char *p = args + 5;
        while (*p == ' ') p++;
        if (*p) {
            /* :copilot login <userCode>  -> confirm */
            cJSON *params = cJSON_CreateObject();
            cJSON_AddStringToObject(params, "userCode", p);
            cp_proto_request("signInConfirm", params, CP_REQ_SIGN_IN_CONFIRM);
        } else {
            /* :copilot login          -> initiate device flow */
            cp_proto_request("signInInitiate", cJSON_CreateObject(),
                             CP_REQ_SIGN_IN_INITIATE);
        }
        return;
    }
    if (strcmp(args, "restart") == 0) {
        cp_proto_shutdown();
        if (cp_proto_spawn() == 0) cp_send_initialize();
        return;
    }
    if (strcmp(args, "pane") == 0) {
        int buf_idx = cp_pane_get_or_create_buf();
        if (buf_idx < 0) { ed_set_status_message("copilot: pane create failed"); return; }
        int win_idx = cp_pane_window_idx(buf_idx);
        if (win_idx >= 0) {
            if (win_idx != E.current_window) {
                E.windows[E.current_window].focus = 0;
                E.windows[win_idx].focus = 1;
                E.current_window = win_idx;
                E.current_buffer = buf_idx;
            }
        } else {
            windows_split_horizontal();
            Window *w = window_cur();
            if (w) win_attach_buf(w, &E.buffers[buf_idx]);
            E.buffers[buf_idx].dirty = 0;
        }
        cp_pane_refresh();
        return;
    }
    if (strcmp(args, "next") == 0) {
        if (CP.alts_count == 0) { ed_set_status_message("copilot: no suggestions"); return; }
        CP.alts_active = (CP.alts_active + 1) % CP.alts_count;
        cp_show_active(CP.suggestion_line, CP.suggestion_col);
        cp_pane_refresh();
        return;
    }
    if (strcmp(args, "prev") == 0) {
        if (CP.alts_count == 0) { ed_set_status_message("copilot: no suggestions"); return; }
        CP.alts_active = (CP.alts_active - 1 + CP.alts_count) % CP.alts_count;
        cp_show_active(CP.suggestion_line, CP.suggestion_col);
        cp_pane_refresh();
        return;
    }
    ed_set_status_message("copilot: unknown subcommand '%s'", args);
}

/* ----- plugin lifecycle ----- */

static int copilot_init(void) {
    memset(&CP, 0, sizeof(CP));
    CP.to_fd = CP.from_fd = -1;
    CP.content_length = -1;

    CP.vt_ns = vtext_ns_create("copilot");
    if (CP.vt_ns >= 0) vtext_ns_set_auto_clear(CP.vt_ns, 0);

    cmd("copilot", cmd_copilot,
        "copilot login [code] | logout | status | enable | disable | "
        "restart | pane | next | prev");

    /* Tab in insert mode: accept suggestion, else insert a literal tab.
     * Esc dismissal is handled via on_mode_change so we don't have to
     * shadow vim_keybinds' Esc binding. */
    mapi("<Tab>", kb_accept, "copilot: accept suggestion (or insert tab)");

    hook_register_char  (HOOK_CHAR_INSERT,  MODE_INSERT, "*", on_char_insert);
    hook_register_char  (HOOK_CHAR_DELETE,  MODE_INSERT, "*", on_char_delete);
    hook_register_cursor(HOOK_CURSOR_MOVE,  -1, "*", on_cursor_move);
    hook_register_mode  (HOOK_MODE_CHANGE,  on_mode_change);
    hook_register_buffer(HOOK_BUFFER_OPEN,  -1, "*", on_buffer_open);
    hook_register_buffer(HOOK_BUFFER_CLOSE, -1, "*", on_buffer_close);

    /* Spawn lazily: only if the binary is reachable. Failure is
     * non-fatal — the plugin stays loaded and reports via status. */
    if (cp_proto_spawn() == 0) cp_send_initialize();

    return 0;
}

static void copilot_deinit(void) {
    ed_loop_timer_cancel("copilot:debounce");
    cp_clear_suggestion();
    cp_proto_shutdown();
}

const Plugin plugin_copilot = {
    .name   = "copilot",
    .desc   = "GitHub Copilot ghost-text suggestions via copilot-language-server",
    .init   = copilot_init,
    .deinit = copilot_deinit,
};

