#include "hed.h"
#include "ts.h"
#include "buffer.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>

#ifdef USE_TREESITTER
#include <tree_sitter/api.h>
#endif

/*
 * Tree-sitter scaffolding.
 * By default, these functions are no-ops unless compiled with USE_TREESITTER.
 */

static int g_ts_enabled = 0;

#ifdef USE_TREESITTER
typedef struct {
    TSParser *parser;
    TSTree   *tree;
    TSLanguage *lang;
    TSQuery *query;
    void *dl_handle;
    char lang_name[32];
} TSState;
#else
typedef struct { int dummy; } TSState;
#endif

void ts_set_enabled(int on) { g_ts_enabled = on ? 1 : 0; }
int  ts_is_enabled(void) { return g_ts_enabled; }

void ts_buffer_init(Buffer *buf) {
    if (!buf) return;
    if (buf->ts_internal) return;
    buf->ts_internal = calloc(1, sizeof(TSState));
}
void ts_buffer_free(Buffer *buf) {
    if (!buf || !buf->ts_internal) return;
#ifdef USE_TREESITTER
    TSState *st = (TSState *)buf->ts_internal;
    if (st->tree) ts_tree_delete(st->tree);
    if (st->parser) ts_parser_delete(st->parser);
    if (st->query) ts_query_delete(st->query);
    if (st->dl_handle) dlclose(st->dl_handle);
#endif
    free(buf->ts_internal);
    buf->ts_internal = NULL;
}

#ifdef USE_TREESITTER
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
#endif

int ts_buffer_load_language(Buffer *buf, const char *lang_name) {
    if (!buf) return 0;
    ts_buffer_init(buf);
#ifdef USE_TREESITTER
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
#else
    (void)lang_name;
    return 0;
#endif
}

int ts_buffer_autoload(Buffer *buf) {
    if (!buf || !buf->filename) return 0;
    /* Detect by extension */
    const char *ext = strrchr(buf->filename, '.');
    if (!ext || !ext[1]) return 0;
    ext++;
    if (strcmp(ext, "c") == 0 || strcmp(ext, "h") == 0) return ts_buffer_load_language(buf, "c");
    return 0;
}

void ts_buffer_reparse(Buffer *buf) {
#ifdef USE_TREESITTER
    if (!buf || !buf->ts_internal) return;
    TSState *st = (TSState *)buf->ts_internal;
    if (!st->parser || !st->lang) return;
    char *src = NULL; size_t len = build_source(buf, &src);
    if (!src) return;
    if (st->tree) ts_tree_delete(st->tree);
    st->tree = ts_parser_parse_string(st->parser, NULL, src, (uint32_t)len);
    free(src);
#else
    (void)buf;
#endif
}

size_t ts_highlight_line(Buffer *buf, int line_index,
                         char *dst, size_t dst_cap,
                         int col_offset, int max_cols) {
    (void)dst_cap;
#ifdef USE_TREESITTER
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

    /* Collect segments colorized; simple strategy: support @string, @comment */
    typedef struct { uint32_t s, e; int color; } Seg;
    Seg segs[128]; int sc = 0;
    TSQueryMatch m;
    while (ts_query_cursor_next_match(cur, &m) && sc < 128) {
        for (uint32_t i = 0; i < m.capture_count && sc < 128; i++) {
            TSQueryCapture c = m.captures[i];
            const char *name; uint32_t nlen;
            name = ts_query_capture_name_for_id(st->query, c.index, &nlen);
            int color = 0;
            if (nlen && strncmp(name, "string", nlen) == 0) color = 36; /* cyan */
            else if (nlen && strncmp(name, "comment", nlen) == 0) color = 90; /* gray */
            else continue;
            uint32_t s = ts_node_start_byte(c.node);
            uint32_t e = ts_node_end_byte(c.node);
            if (e <= start || s >= end) continue;
            if (s < start) s = (uint32_t)start;
            if (e > end) e = (uint32_t)end;
            segs[sc++] = (Seg){ s, e, color };
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
            char esc[16]; int el = snprintf(esc, sizeof(esc), "\x1b[%dm", segs[si].color);
            if (out + el < dst_cap) { memcpy(dst+out, esc, el); out += el; }
            while (rem > 0 && pos < segs[si].e && pos < line_start + (uint32_t)linelen) {
                char ch = line[pos - line_start];
                if (out + 1 < dst_cap) dst[out++] = ch;
                pos++; rem--;
            }
            const char *reset="\x1b[0m"; if (out + 4 < dst_cap) { memcpy(dst+out, reset, 4); out += 4; }
            colored = 1;
        }
        if (!colored && rem > 0 && pos < line_start + (uint32_t)linelen) {
            char ch = line[pos - line_start];
            if (out + 1 < dst_cap) dst[out++] = ch;
            pos++; rem--;
        }
    }
    return out;
#else
    (void)buf; (void)line_index; (void)col_offset; (void)max_cols; (void)dst;
    return 0;
#endif
}
