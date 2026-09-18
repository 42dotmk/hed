/* WorkspaceEdit / TextEdit application: the one place server-supplied
 * edits (code actions, rename, formatting, workspace/applyEdit) turn
 * into buffer mutations. Pure buffer surgery — no server state. */

#include "lsp_edit.h"
#include "hed.h"
#include "json_helpers.h"
#include "lsp.h"

/* ------------------------------------------------- UTF-16 positions */

int lsp_cx_to_utf16(const char *s, size_t len, int cx) {
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

int lsp_utf16_to_cx(const char *s, size_t len, int u16) {
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

/* ------------------------------------------------- uri → buffer */

/* "file:///a%20b" → "/a b". Only the path part; no host support. */
static void lsp_uri_decode_path(const char *uri, char *out, size_t sz) {
    const char *p = fs_uri_to_path(uri);
    size_t w = 0;
    while (*p && w + 1 < sz) {
        if (p[0] == '%' && isxdigit((unsigned char)p[1]) &&
            isxdigit((unsigned char)p[2])) {
            char hex[3] = {p[1], p[2], 0};
            out[w++] = (char)strtol(hex, NULL, 16);
            p += 3;
        } else {
            out[w++] = *p++;
        }
    }
    out[w] = '\0';
}

static int lsp_buffer_matches_uri(const Buffer *b, const char *uri) {
    if (!b->filename)
        return 0;
    if (strcmp(b->filename, uri) == 0)
        return 1; /* virtual document: filename is the URI */
    char *u = fs_path_to_file_uri(b->filename, NULL);
    int eq = u && strcmp(u, uri) == 0;
    free(u);
    return eq;
}

int lsp_buffer_for_uri(const char *uri, int open_missing) {
    if (!uri || !*uri)
        return -1;
    /* The current buffer first: a file opened once by a relative and
     * once by an absolute name has two buffers for one URI, and the
     * one on screen is the one the user means. */
    Buffer *cur = buf_cur();
    if (cur && lsp_buffer_matches_uri(cur, uri))
        return (int)(cur - E.buffers);
    for (int i = 0; i < (int)arrlen(E.buffers); i++) {
        if (lsp_buffer_matches_uri(&E.buffers[i], uri))
            return i;
    }
    if (!open_missing || strncmp(uri, "file://", 7) != 0)
        return -1;
    char path[PATH_MAX];
    lsp_uri_decode_path(uri, path, sizeof(path));
    Buffer *out = NULL;
    if (buf_open_file(path, &out) != ED_OK || !out)
        return -1;
    return (int)(out - E.buffers);
}

/* ------------------------------------------------- text edits */

/* An LSP Position resolved onto `buf` in byte coordinates. A position
 * at or past the end of the document clamps to the end of the last row
 * and sets `past_end` — the range then swallows the trailing newline
 * the server sees after the last row (buf_to_text emits one per row). */
typedef struct {
    int line, col;
    int past_end;
} EdPos;

static EdPos lsp_pos_resolve(const Buffer *buf, const cJSON *pos) {
    EdPos r = {0, 0, 0};
    int line = json_get_int(pos, "line", 0);
    int ch = json_get_int(pos, "character", 0);
    if (buf->num_rows == 0) {
        r.past_end = 1;
        return r;
    }
    if (line < 0)
        line = 0;
    if (line >= buf->num_rows) {
        r.line = buf->num_rows - 1;
        r.col = (int)buf->rows[r.line].chars.len;
        r.past_end = 1;
        return r;
    }
    const Row *row = &buf->rows[line];
    r.line = line;
    r.col = lsp_utf16_to_cx(row->chars.data, row->chars.len, ch);
    return r;
}

static void lsp_row_set(Buffer *buf, int idx, const char *s, size_t len) {
    Row *row = &buf->rows[idx];
    undo_record_replace(buf, idx);
    strbuf_free(&row->chars);
    row->chars = strbuf_from(s, len);
    buf_row_update(row);
    buf->dirty++;
}

/* Replace [start, end) with `text`. Works on the composed string
 * prefix + text + suffix and re-splits it into rows, so multi-line
 * edits, edits spanning rows and edits that add or remove rows all go
 * through one path. */
static void lsp_apply_one(Buffer *buf, EdPos s, EdPos e, const char *text) {
    if (!text)
        text = "";
    StrBuf composed = strbuf_new();
    int sl = s.line, el = e.line;
    if (buf->num_rows == 0) {
        strbuf_append(&composed, text, strlen(text));
        sl = 0;
        el = -1; /* nothing to replace */
    } else {
        const Row *first = &buf->rows[sl];
        strbuf_append(&composed, first->chars.data, (size_t)s.col);
        if (s.past_end) /* start sits after the last row's newline */
            strbuf_append_char(&composed, '\n');
        strbuf_append(&composed, text, strlen(text));
        if (!e.past_end) {
            const Row *last = &buf->rows[el];
            strbuf_append(&composed, last->chars.data + e.col,
                          last->chars.len - (size_t)e.col);
            strbuf_append_char(&composed, '\n');
        }
    }

    /* Split into rows; a trailing '\n' closes the last row rather than
     * opening an empty one (same convention as buf_to_text). */
    int nold = el - sl + 1;
    int nnew = 0;
    const char *p = composed.data ? composed.data : "";
    const char *end = p + composed.len;
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
        if (len && p[len - 1] == '\r')
            len--;
        if (nnew < nold)
            lsp_row_set(buf, sl + nnew, p, len);
        else
            buf_row_insert_in(buf, sl + nnew, p, len);
        nnew++;
        p = nl ? nl + 1 : end;
    }
    for (int i = sl + nold - 1; i >= sl + nnew; i--)
        buf_row_del_in(buf, i);
    strbuf_free(&composed);
}

typedef struct {
    int line, ch, idx;
    cJSON *edit;
} EdSort;

/* Bottom-up, and for equal starts later-in-array first, so that two
 * inserts at one position land in array order (spec: "the order in
 * the array defines the order in which the inserted strings appear"). */
static int lsp_edit_cmp(const void *a, const void *b) {
    const EdSort *x = a, *y = b;
    if (x->line != y->line)
        return y->line - x->line;
    if (x->ch != y->ch)
        return y->ch - x->ch;
    return y->idx - x->idx;
}

static void lsp_clamp_cursor(const Buffer *buf, Cursor *c) {
    if (buf->num_rows == 0) {
        c->y = c->x = 0;
        return;
    }
    if (c->y >= buf->num_rows)
        c->y = buf->num_rows - 1;
    if (c->y < 0)
        c->y = 0;
    int len = (int)buf->rows[c->y].chars.len;
    if (c->x > len)
        c->x = len;
    if (c->x < 0)
        c->x = 0;
}

static void lsp_clamp_cursors(int buf_idx) {
    Buffer *buf = &E.buffers[buf_idx];
    for (ptrdiff_t i = 0; i < arrlen(buf->all_cursors); i++)
        lsp_clamp_cursor(buf, buf->all_cursors[i]);
    for (ptrdiff_t i = 0; i < arrlen(E.windows); i++) {
        Window *w = &E.windows[i];
        if (w->buffer_index == buf_idx)
            lsp_clamp_cursor(buf, &w->cursor);
    }
}

int lsp_apply_text_edits(int buf_idx, cJSON *edits, const char *desc) {
    if (buf_idx < 0 || buf_idx >= (int)arrlen(E.buffers) || !edits ||
        !cJSON_IsArray(edits))
        return 0;
    int n = cJSON_GetArraySize(edits);
    if (n <= 0)
        return 0;
    Buffer *buf = &E.buffers[buf_idx];
    if (buf->readonly) {
        ed_set_status_message("LSP: %s is read-only", buf->title);
        return 0;
    }

    EdSort *order = calloc((size_t)n, sizeof(EdSort));
    if (!order)
        return 0;
    int cnt = 0;
    for (int i = 0; i < n; i++) {
        cJSON *ed = cJSON_GetArrayItem(edits, i);
        cJSON *range = json_get_object(ed, "range");
        cJSON *start = range ? json_get_object(range, "start") : NULL;
        if (!start || !json_get_object(range, "end"))
            continue;
        order[cnt++] = (EdSort){
            .line = json_get_int(start, "line", 0),
            .ch = json_get_int(start, "character", 0),
            .idx = i,
            .edit = ed,
        };
    }
    qsort(order, (size_t)cnt, sizeof(EdSort), lsp_edit_cmp);

    undo_begin(buf, desc ? desc : "lsp edit");
    int applied = 0;
    for (int i = 0; i < cnt; i++) {
        cJSON *range = json_get_object(order[i].edit, "range");
        EdPos s = lsp_pos_resolve(buf, json_get_object(range, "start"));
        EdPos e = lsp_pos_resolve(buf, json_get_object(range, "end"));
        if (e.line < s.line || (e.line == s.line && e.col < s.col))
            e = s;
        const char *text = json_get_string(order[i].edit, "newText");
        lsp_apply_one(buf, s, e, text);
        applied++;
    }
    undo_end(buf);
    free(order);

    lsp_clamp_cursors(buf_idx);
    lsp_sync_document(buf);
    return applied;
}

/* ------------------------------------------------- workspace edits */

static int lsp_apply_document(const char *uri, cJSON *edits, const char *desc,
                              int *files) {
    if (!uri || !edits)
        return 0;
    int idx = lsp_buffer_for_uri(uri, 1);
    if (idx < 0) {
        log_msg("LSP: workspace edit: cannot open %s", uri);
        ed_set_status_message("LSP: cannot open %s", fs_uri_to_path(uri));
        return 0;
    }
    int n = lsp_apply_text_edits(idx, edits, desc);
    if (n > 0)
        (*files)++;
    return n;
}

int lsp_apply_workspace_edit(cJSON *edit, const char *desc, int *out_files) {
    int total = 0, files = 0;
    if (!edit || !cJSON_IsObject(edit))
        return 0;

    cJSON *dc = json_get_array(edit, "documentChanges");
    if (dc) {
        /* TextDocumentEdit | CreateFile | RenameFile | DeleteFile */
        for (int i = 0; i < cJSON_GetArraySize(dc); i++) {
            cJSON *item = cJSON_GetArrayItem(dc, i);
            cJSON *tdoc = json_get_object(item, "textDocument");
            if (tdoc) {
                total += lsp_apply_document(json_get_string(tdoc, "uri"),
                                            json_get_array(item, "edits"), desc,
                                            &files);
                continue;
            }
            const char *kind = json_get_string(item, "kind");
            log_msg("LSP: workspace edit: skipping resource op '%s'",
                    kind ? kind : "?");
        }
    } else {
        cJSON *changes = json_get_object(edit, "changes");
        for (cJSON *c = changes ? changes->child : NULL; c; c = c->next) {
            if (cJSON_IsArray(c))
                total += lsp_apply_document(c->string, c, desc, &files);
        }
    }
    if (out_files)
        *out_files = files;
    return total;
}
