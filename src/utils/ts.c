#include "hed.h"
#include "ts.h"
#include "buffer.h"
#include "log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <tree_sitter/api.h>

/*
 * Tree-sitter integration: always compiled in.
 */

static int g_ts_enabled = 1;

typedef struct {
    TSParser *parser;
    TSTree   *tree;
    TSLanguage *lang;
    TSQuery *query;
    void *dl_handle;
    char lang_name[32];
} TSState;

void ts_set_enabled(int on) { g_ts_enabled = on ? 1 : 0; }
int  ts_is_enabled(void) { return g_ts_enabled; }

void ts_buffer_init(Buffer *buf) {
    if (!buf) return;
    if (buf->ts_internal) return;
    buf->ts_internal = calloc(1, sizeof(TSState));
}
void ts_buffer_free(Buffer *buf) {
    if (!buf || !buf->ts_internal) return;
    TSState *st = (TSState *)buf->ts_internal;
    if (st->tree) ts_tree_delete(st->tree);
    if (st->parser) ts_parser_delete(st->parser);
    if (st->query) ts_query_delete(st->query);
    if (st->dl_handle) dlclose(st->dl_handle);
    free(buf->ts_internal);
    buf->ts_internal = NULL;
}

static size_t build_source(Buffer *buf, char **out) {
    size_t total = 0;
    for (int i = 0; i < buf->num_rows; i++) total += buf->rows[i].chars.len + 1;
    char *s = malloc(total + 1);
    size_t off = 0;
    for (int i = 0; i < buf->num_rows; i++) {
        memcpy(s + off, buf->rows[i].chars.data, buf->rows[i].chars.len);
        off += buf->rows[i].chars.len;
        s[off++] = '\n';
    }
    s[off] = '\0';
    *out = s;
    return off;
}

static int load_lang(TSState *st, const char *lang_name) {
    if (!lang_name || !*lang_name) return 0;
    char path[512];
    const char *base = getenv("HED_TS_PATH");
    if (base && *base) snprintf(path, sizeof(path), "%s/%s.so", base, lang_name);
    else snprintf(path, sizeof(path), "ts-langs/%s.so", lang_name);
    void *h = dlopen(path, RTLD_NOW);
    if (!h) return 0;
    char sym[64]; snprintf(sym, sizeof(sym), "tree_sitter_%s", lang_name);
    TSLanguage *(*langfn)(void) = (TSLanguage *(*)(void))dlsym(h, sym);
    if (!langfn) { dlclose(h); return 0; }
    st->lang = langfn();
    st->dl_handle = h;
    strncpy(st->lang_name, lang_name, sizeof(st->lang_name)-1);
    return 1;
}

static void maybe_load_query(TSState *st, const char *lang) {
    char qpath[512];
    snprintf(qpath, sizeof(qpath), "queries/%s/highlights.scm", lang);
    FILE *fp = fopen(qpath, "r");
    if (!fp) return;
    fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    fread(buf, 1, (size_t)sz, fp); buf[sz] = '\0'; fclose(fp);
    uint32_t err_offset; TSQueryError err_type;
    st->query = ts_query_new(st->lang, buf, (uint32_t)sz, &err_offset, &err_type);
    free(buf);
}

int ts_buffer_load_language(Buffer *buf, const char *lang_name) {
    if (!buf) return 0;
    log_msg("Loading tree-sitter language: %s for buf: %s", lang_name, buf->title);
    ts_buffer_init(buf);
    log_msg("tree-sitter is enabled for buf: %s", buf->title);
    TSState *st = (TSState *)buf->ts_internal;
    if (!st) return 0;
    if (st->parser) { ts_parser_delete(st->parser); st->parser = NULL; }
    if (st->tree) { ts_tree_delete(st->tree); st->tree = NULL; }
    if (st->query) { ts_query_delete(st->query); st->query = NULL; }
    if (st->dl_handle) { dlclose(st->dl_handle); st->dl_handle = NULL; }
    st->lang = NULL; st->lang_name[0] = '\0';
    if (!load_lang(st, lang_name)) return 0;
    st->parser = ts_parser_new();
    if (!ts_parser_set_language(st->parser, st->lang)) return 0;
    maybe_load_query(st, lang_name);
    ts_buffer_reparse(buf);
    return 1;
}

int ts_buffer_autoload(Buffer *buf) {
    if (!buf || !buf->filename) return 0;
    /* Detect by extension */
    const char *ext = strrchr(buf->filename, '.');
    if (!ext || !ext[1]) return 0;
    ext++;

    /* Core C / C++ */
    if (strcmp(ext, "c") == 0 || strcmp(ext, "h") == 0)
        return ts_buffer_load_language(buf, "c");
    if (strcmp(ext, "cpp") == 0 || strcmp(ext, "cc") == 0 ||
        strcmp(ext, "cxx") == 0 || strcmp(ext, "hpp") == 0 ||
        strcmp(ext, "hh") == 0  || strcmp(ext, "hxx") == 0)
        return ts_buffer_load_language(buf, "cpp");

    /* C# */
    if (strcmp(ext, "cs") == 0)
        return ts_buffer_load_language(buf, "c-sharp");

    /* Make */
    if (strcmp(buf->filename, "makefile") == 0 || strcmp(buf->filename, "Makefile") == 0)
        return ts_buffer_load_language(buf, "make");

    /* Python */
    if (strcmp(ext, "py") == 0)
        return ts_buffer_load_language(buf, "python");

    /* HTML */
    if (strcmp(ext, "html") == 0 || strcmp(ext, "htm") == 0)
        return ts_buffer_load_language(buf, "html");

    /* Go */
    if (strcmp(ext, "go") == 0)
        return ts_buffer_load_language(buf, "go");

    /* JavaScript / TypeScript */
    if (strcmp(ext, "js") == 0)
        return ts_buffer_load_language(buf, "javascript");
    if (strcmp(ext, "ts") == 0 || strcmp(ext, "tsx") == 0)
        return ts_buffer_load_language(buf, "typescript");

    /* Rust */
    if (strcmp(ext, "rs") == 0)
        return ts_buffer_load_language(buf, "rust");

    /* Lua */
    if (strcmp(ext, "lua") == 0)
        return ts_buffer_load_language(buf, "lua");

    /* Shell */
    if (strcmp(ext, "sh") == 0 || strcmp(ext, "bash") == 0 || strcmp(ext, "zsh") == 0)
        return ts_buffer_load_language(buf, "bash");

    /* JSON */
    if (strcmp(ext, "json") == 0)
        return ts_buffer_load_language(buf, "json");

    /* YAML */
    if (strcmp(ext, "yml") == 0 || strcmp(ext, "yaml") == 0)
        return ts_buffer_load_language(buf, "yaml");

    /* TOML */
    if (strcmp(ext, "toml") == 0)
        return ts_buffer_load_language(buf, "toml");

    /* Markdown */
    if (strcmp(ext, "md") == 0 || strcmp(ext, "markdown") == 0)
        return ts_buffer_load_language(buf, "markdown");

    return 0;
}

void ts_buffer_reparse(Buffer *buf) {
    if (!buf || !buf->ts_internal) return;
    TSState *st = (TSState *)buf->ts_internal;
    if (!st->parser || !st->lang) return;
    char *src = NULL; size_t len = build_source(buf, &src);
    if (!src) return;
    if (st->tree) ts_tree_delete(st->tree);
    st->tree = ts_parser_parse_string(st->parser, NULL, src, (uint32_t)len);
    free(src);
}

size_t ts_highlight_line(Buffer *buf, int line_index,
                         char *dst, size_t dst_cap,
                         int col_offset, int max_cols) {
    (void)dst_cap;
    if (!g_ts_enabled || !buf || !buf->ts_internal) return 0;
    TSState *st = (TSState *)buf->ts_internal;
    if (!st->tree || !st->query) return 0;
    /* Compute byte range for this line */
    size_t start = 0;
    for (int i = 0; i < line_index; i++) start += buf->rows[i].chars.len + 1;
    size_t end = start + buf->rows[line_index].chars.len;

    TSNode root = ts_tree_root_node(st->tree);
    TSQueryCursor *cur = ts_query_cursor_new();
    ts_query_cursor_exec(cur, st->query, root);
    ts_query_cursor_set_byte_range(cur, (uint32_t)start, (uint32_t)end);

    /* Collect segments colorized.
     * We support common capture names from tree-sitter-c's highlights:
     *  @string, @comment, @variable, @constant, @number, @keyword,
     *  @type, @function, @property, @label, @operator, @delimiter, etc.
     */
    typedef struct { uint32_t s, e; const char *sgr; } Seg;
    Seg segs[128]; int sc = 0;
    TSQueryMatch m;
    while (ts_query_cursor_next_match(cur, &m) && sc < 128) {
        for (uint32_t i = 0; i < m.capture_count && sc < 128; i++) {
            TSQueryCapture c = m.captures[i];
            const char *name; uint32_t nlen;
            name = ts_query_capture_name_for_id(st->query, c.index, &nlen);
            const char *sgr = NULL;

            /* Match by prefix so dotted captures like "string.regex"
             * or "function.builtin" still get colored.
             */
            if (nlen >= 6 && strncmp(name, "string", 6) == 0) {
                sgr = COLOR_STRING;
            } else if (nlen >= 6 && strncmp(name, "escape", 6) == 0) {
                /* @escape, @string.escape, etc. */
                sgr = COLOR_STRING;
            } else if (nlen >= 7 && strncmp(name, "comment", 7) == 0) {
                sgr = COLOR_COMMENT;
            } else if (nlen >= 8 && strncmp(name, "variable", 8) == 0) {
                sgr = COLOR_VARIABLE;
            } else if (nlen >= 8 && strncmp(name, "constant", 8) == 0) {
                /* includes constant.builtin, constant.macro */
                sgr = COLOR_CONSTANT;
            } else if (nlen >= 6 && strncmp(name, "number", 6) == 0) {
                sgr = COLOR_NUMBER;
            } else if ((nlen >= 7 && strncmp(name, "keyword", 7) == 0) ||
                       (nlen >= 11 && strncmp(name, "conditional", 11) == 0) ||
                       (nlen >= 6 && strncmp(name, "repeat", 6) == 0) ||
                       (nlen >= 7 && strncmp(name, "include", 7) == 0)) {
                /* keyword, keyword.function, conditional, repeat, include */
                sgr = COLOR_KEYWORD;
            } else if ((nlen >= 4 && strncmp(name, "type", 4) == 0) ||
                       (nlen >= 6 && strncmp(name, "module", 6) == 0) ||
                       (nlen >= 11 && strncmp(name, "constructor", 11) == 0)) {
                /* type, type.builtin, constructor, module */
                sgr = COLOR_TYPE;
            } else if (nlen >= 8 && strncmp(name, "function", 8) == 0) {
                /* covers function, function.method, function.builtin, function.special */
                sgr = COLOR_FUNCTION;
            } else if ((nlen >= 8 && strncmp(name, "property", 8) == 0) ||
                       (nlen >= 9 && strncmp(name, "attribute", 9) == 0)) {
                /* property, property.definition, attribute */
                sgr = COLOR_ATTRIBUTE;
            } else if (nlen >= 5 && strncmp(name, "label", 5) == 0) {
                sgr = COLOR_LABEL;
            } else if (nlen >= 8 && strncmp(name, "operator", 8) == 0) {
                sgr = COLOR_OPERATOR;
            } else if ((nlen >= 11 && strncmp(name, "punctuation", 11) == 0) ||
                       (nlen >= 9 && strncmp(name, "delimiter", 9) == 0)) {
                /* punctuation.bracket, punctuation.delimiter, punctuation.special, delimiter */
                sgr = COLOR_PUNCT;
            } else if (nlen >= 4 && strncmp(name, "text", 4) == 0) {
                /* text.danger, text.warning, text.note */
                if (nlen >= 11 && strncmp(name, "text.danger", 11) == 0)
                    sgr = COLOR_DIAG_ERROR;
                else if (nlen >= 12 && strncmp(name, "text.warning", 12) == 0)
                    sgr = COLOR_DIAG_WARN;
                else if (nlen >= 10 && strncmp(name, "text.note", 9) == 0)
                    sgr = COLOR_DIAG_NOTE;
                else
                    sgr = COLOR_COMMENT;
            } else if (nlen >= 9 && strncmp(name, "exception", 9) == 0) {
                /* exception buckets: treat as errors */
                sgr = COLOR_DIAG_ERROR;
            } else {
                continue;
            }
            uint32_t s = ts_node_start_byte(c.node);
            uint32_t e = ts_node_end_byte(c.node);
            if (e <= start || s >= end) continue;
            if (s < start) s = (uint32_t)start;
            if (e > end) e = (uint32_t)end;
            segs[sc++] = (Seg){ s, e, sgr };
        }
    }
    ts_query_cursor_delete(cur);

    /* Render line with ANSI segments */
    const char *line = buf->rows[line_index].render.data;
    int linelen = (int)buf->rows[line_index].render.len;
    int x = col_offset;
    if (x < 0) x = 0; if (x > linelen) x = linelen;
    int rem = max_cols;
    size_t out = 0;
    uint32_t line_start = (uint32_t)start;
    /* sort segs by start */
    for (int i = 0; i < sc; i++) for (int j = i+1; j < sc; j++) if (segs[j].s < segs[i].s) { Seg t = segs[i]; segs[i]=segs[j]; segs[j]=t; }
    int si = 0;
    uint32_t pos = line_start + (uint32_t)x;
    while (rem > 0 && pos < line_start + (uint32_t)linelen) {
        int colored = 0;
        while (si < sc && segs[si].e <= pos) si++;
        if (si < sc && segs[si].s <= pos && pos < segs[si].e) {
            /* inside colored seg */
            size_t el = strlen(segs[si].sgr);
            if (out + el < dst_cap) { memcpy(dst+out, segs[si].sgr, el); out += el; }
            while (rem > 0 && pos < segs[si].e && pos < line_start + (uint32_t)linelen) {
                char ch = line[pos - line_start];
                if (out + 1 < dst_cap) dst[out++] = ch;
                pos++; rem--;
            }
            const char *reset = COLOR_RESET;
            size_t rl = strlen(reset);
            if (out + rl < dst_cap) { memcpy(dst+out, reset, rl); out += rl; }
            colored = 1;
        }
        if (!colored && rem > 0 && pos < line_start + (uint32_t)linelen) {
            char ch = line[pos - line_start];
            if (out + 1 < dst_cap) dst[out++] = ch;
            pos++; rem--;
        }
    }
    return out;
}
