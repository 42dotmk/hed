#include "ts.h"
#include "hed.h"
#include <dlfcn.h>
#include <limits.h>
#include <tree_sitter/api.h>

/*
 * Tree-sitter integration with language injection support.
 *
 * Each buffer has a host TSState. If the host's grammar ships an
 * injections.scm, sub-languages identified by injection captures are
 * loaded on demand and parsed against byte ranges within the same source
 * via ts_parser_set_included_ranges. Highlights are then merged: outside
 * injection ranges the host highlight query wins, inside, the
 * sub-language's highlights.scm is used.
 */

static int g_ts_enabled = 1;

typedef struct {
    char     lang_name[32];
    uint32_t start_byte;
    uint32_t end_byte;
} TSInjectionRange;

typedef struct {
    char        lang_name[32];
    TSLanguage *lang;
    TSParser   *parser;
    TSTree     *tree;
    TSQuery    *query;
    void       *dl_handle;
    int         load_failed; /* 1 once we know this lang can't be loaded */
} TSSubLang;

typedef struct {
    TSParser   *parser;
    TSTree     *tree;
    TSLanguage *lang;
    TSQuery    *query;
    TSQuery    *inject_query;
    void       *dl_handle;
    char        lang_name[32];
    int         parsed_dirty; /* last buf->dirty value parsed; -1 = needs parse */

    TSInjectionRange *injections;
    int               num_injections;
    int               cap_injections;

    TSSubLang        *sub_langs;
    int               num_sub_langs;
    int               cap_sub_langs;
} TSState;

void ts_set_enabled(int on) { g_ts_enabled = on ? 1 : 0; }
int ts_is_enabled(void) { return g_ts_enabled; }

void ts_buffer_init(Buffer *buf) {
    if (!buf)
        return;
    if (buf->ts_internal)
        return;
    buf->ts_internal = calloc(1, sizeof(TSState));
    if (buf->ts_internal) {
        ((TSState *)buf->ts_internal)->parsed_dirty = -1;
    }
}

static void free_sub_lang(TSSubLang *s) {
    if (!s)
        return;
    if (s->tree)
        ts_tree_delete(s->tree);
    if (s->parser)
        ts_parser_delete(s->parser);
    if (s->query)
        ts_query_delete(s->query);
    if (s->dl_handle)
        dlclose(s->dl_handle);
    memset(s, 0, sizeof(*s));
}

void ts_buffer_free(Buffer *buf) {
    if (!buf || !buf->ts_internal)
        return;
    TSState *st = (TSState *)buf->ts_internal;
    if (st->tree)
        ts_tree_delete(st->tree);
    if (st->parser)
        ts_parser_delete(st->parser);
    if (st->query)
        ts_query_delete(st->query);
    if (st->inject_query)
        ts_query_delete(st->inject_query);
    if (st->dl_handle)
        dlclose(st->dl_handle);
    if (st->injections)
        free(st->injections);
    if (st->sub_langs) {
        for (int i = 0; i < st->num_sub_langs; i++)
            free_sub_lang(&st->sub_langs[i]);
        free(st->sub_langs);
    }
    free(buf->ts_internal);
    buf->ts_internal = NULL;
}

static size_t build_source(Buffer *buf, char **out) {
    size_t total = 0;
    for (int i = 0; i < buf->num_rows; i++)
        total += buf->rows[i].chars.len + 1;
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

static void ts_default_base(char *out, size_t out_sz) {
    if (!out || out_sz == 0)
        return;
    out[0] = '\0';

    const char *env_base = getenv("HED_TS_PATH");
    if (env_base && *env_base) {
        snprintf(out, out_sz, "%s", env_base);
        return;
    }

    const char *xdg_config = getenv("XDG_CONFIG_HOME");
    const char *xdg_home = getenv("XDG_HOME");
    if (xdg_config && *xdg_config) {
        snprintf(out, out_sz, "%s/hed/ts", xdg_config);
        return;
    }
    if (!xdg_home || !*xdg_home)
        xdg_home = getenv("HOME");
    if (xdg_home && *xdg_home) {
        snprintf(out, out_sz, "%s/.config/hed/ts", xdg_home);
        return;
    }

    snprintf(out, out_sz, "ts-langs");
}

/* Load a tree-sitter language .so and return its TSLanguage* and dl handle. */
static int load_lang_dl(const char *lang_name, TSLanguage **out_lang,
                        void **out_handle) {
    if (!lang_name || !*lang_name)
        return 0;
    char path[PATH_MAX];
    char base[PATH_MAX];
    ts_default_base(base, sizeof(base));
    if (base[0])
        snprintf(path, sizeof(path), "%s/%s.so", base, lang_name);
    else
        snprintf(path, sizeof(path), "ts/%s.so", lang_name);
    void *h = dlopen(path, RTLD_NOW);
    if (!h) {
        log_msg("TS dlopen failed for lang %s: %s", lang_name, dlerror());
        return 0;
    }

    char sym[64];
    snprintf(sym, sizeof(sym), "tree_sitter_%s", lang_name);
    TSLanguage *(*langfn)(void) = (TSLanguage * (*)(void)) dlsym(h, sym);
    if (!langfn) {
        dlclose(h);
        return 0;
    }
    *out_lang = langfn();
    *out_handle = h;
    return 1;
}

static TSQuery *load_query_file(TSLanguage *lang, const char *qpath) {
    if (!qpath || !*qpath)
        return NULL;
    FILE *fp = fopen(qpath, "r");
    if (!fp)
        return NULL;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    fread(buf, 1, (size_t)sz, fp);
    buf[sz] = '\0';
    fclose(fp);
    uint32_t err_offset;
    TSQueryError err_type;
    TSQuery *q =
        ts_query_new(lang, buf, (uint32_t)sz, &err_offset, &err_type);
    if (!q) {
        log_msg("TS query parse error in %s at offset %u (err=%d)", qpath,
                err_offset, (int)err_type);
    }
    free(buf);
    return q;
}

/* Try {base}/queries/<lang>/<qname>, then queries/<lang>/<qname>. */
static TSQuery *load_lang_query(TSLanguage *lang, const char *lang_name,
                                const char *qname) {
    char base[PATH_MAX];
    ts_default_base(base, sizeof(base));
    char qpath[PATH_MAX];
    if (base[0]) {
        snprintf(qpath, sizeof(qpath), "%s/queries/%s/%s", base, lang_name,
                 qname);
        TSQuery *q = load_query_file(lang, qpath);
        if (q)
            return q;
    }
    snprintf(qpath, sizeof(qpath), "queries/%s/%s", lang_name, qname);
    return load_query_file(lang, qpath);
}

static int ts_lang_is_loaded(TSState *st, const char *lang_name) {
    if (!st || !st->lang || !st->parser)
        return 0;
    return strcmp(st->lang_name, lang_name) == 0;
}

int ts_buffer_load_language(Buffer *buf, const char *lang_name) {
    if (!buf)
        return 0;
    ts_buffer_init(buf);
    TSState *st = (TSState *)buf->ts_internal;
    if (!st)
        return 0;

    if (ts_lang_is_loaded(st, lang_name)) {
        st->parsed_dirty = -1;
        return 1;
    }

    log_msg("Loading tree-sitter language: %s for buf: %s", lang_name,
            buf->title);

    if (st->parser) {
        ts_parser_delete(st->parser);
        st->parser = NULL;
    }
    if (st->tree) {
        ts_tree_delete(st->tree);
        st->tree = NULL;
    }
    if (st->query) {
        ts_query_delete(st->query);
        st->query = NULL;
    }
    if (st->inject_query) {
        ts_query_delete(st->inject_query);
        st->inject_query = NULL;
    }
    if (st->dl_handle) {
        dlclose(st->dl_handle);
        st->dl_handle = NULL;
    }
    st->lang = NULL;
    st->lang_name[0] = '\0';
    st->parsed_dirty = -1;

    /* The previous host's sub-language assumptions don't carry over. */
    if (st->sub_langs) {
        for (int i = 0; i < st->num_sub_langs; i++)
            free_sub_lang(&st->sub_langs[i]);
        st->num_sub_langs = 0;
    }
    st->num_injections = 0;

    if (!load_lang_dl(lang_name, &st->lang, &st->dl_handle))
        return 0;
    strncpy(st->lang_name, lang_name, sizeof(st->lang_name) - 1);
    st->parser = ts_parser_new();
    if (!ts_parser_set_language(st->parser, st->lang))
        return 0;
    st->query = load_lang_query(st->lang, lang_name, "highlights.scm");
    st->inject_query = load_lang_query(st->lang, lang_name, "injections.scm");
    if (st->inject_query)
        log_msg("TS: loaded injections.scm for %s", lang_name);
    ts_buffer_reparse(buf);
    return 1;
}

int ts_buffer_autoload(Buffer *buf) {
    if (!buf || !buf->filename)
        return 0;
    ts_buffer_init(buf);
    TSState *st = (TSState *)buf->ts_internal;
    const char *want = NULL;

    /* Detect by extension */
    const char *ext = strrchr(buf->filename, '.');
    if (ext && ext[1]) {
        ext++;

        if (strcmp(ext, "c") == 0 || strcmp(ext, "h") == 0)
            want = "c";
        else if (strcmp(ext, "cpp") == 0 || strcmp(ext, "cc") == 0 ||
                 strcmp(ext, "cxx") == 0 || strcmp(ext, "hpp") == 0 ||
                 strcmp(ext, "hh") == 0 || strcmp(ext, "hxx") == 0)
            want = "cpp";
        else if (strcmp(ext, "cs") == 0)
            want = "c-sharp";
        else if (strcmp(ext, "py") == 0)
            want = "python";
        else if (strcmp(ext, "el") == 0 || strcmp(ext, "elisp") == 0 ||
                 strcmp(ext, "lisp") == 0 || strcmp(ext, "cl") == 0)
            want = "commonlisp";
        else if (strcmp(ext, "html") == 0 || strcmp(ext, "htm") == 0)
            want = "html";
        else if (strcmp(ext, "org") == 0 || strcmp(ext, "org_archive") == 0)
            want = "org";
        else if (strcmp(ext, "go") == 0)
            want = "go";
        else if (strcmp(ext, "js") == 0)
            want = "javascript";
        else if (strcmp(ext, "ts") == 0 || strcmp(ext, "tsx") == 0)
            want = "typescript";
        else if (strcmp(ext, "rs") == 0)
            want = "rust";
        else if (strcmp(ext, "lua") == 0)
            want = "lua";
        else if (strcmp(ext, "sh") == 0 || strcmp(ext, "bash") == 0 ||
                 strcmp(ext, "zsh") == 0)
            want = "bash";
        else if (strcmp(ext, "json") == 0)
            want = "json";
        else if (strcmp(ext, "yml") == 0 || strcmp(ext, "yaml") == 0)
            want = "yaml";
        else if (strcmp(ext, "toml") == 0)
            want = "toml";
        else if (strcmp(ext, "md") == 0 || strcmp(ext, "markdown") == 0)
            want = "markdown";
    }

    if (!want && (strcmp(buf->filename, "makefile") == 0 ||
                  strcmp(buf->filename, "Makefile") == 0))
        want = "make";

    if (!want)
        return 0;
    if (ts_lang_is_loaded(st, want))
        return 1;
    return ts_buffer_load_language(buf, want);
}

/* ===================================================================
 * Predicate helper: read `(#set! KEY "VALUE")` directives.
 * =================================================================== */
static int find_set_string_value(const TSQuery *q, uint32_t pattern_idx,
                                 const char *key, char *out, size_t out_sz) {
    uint32_t step_count = 0;
    const TSQueryPredicateStep *steps =
        ts_query_predicates_for_pattern(q, pattern_idx, &step_count);
    if (!steps)
        return 0;
    uint32_t key_len = (uint32_t)strlen(key);

    uint32_t i = 0;
    while (i < step_count) {
        uint32_t j = i;
        while (j < step_count &&
               steps[j].type != TSQueryPredicateStepTypeDone)
            j++;
        /* Predicate occupies [i, j); next starts at j+1. */
        if (j >= i + 3 && steps[i].type == TSQueryPredicateStepTypeString &&
            steps[i + 1].type == TSQueryPredicateStepTypeString &&
            steps[i + 2].type == TSQueryPredicateStepTypeString) {
            uint32_t nlen = 0;
            const char *name =
                ts_query_string_value_for_id(q, steps[i].value_id, &nlen);
            if (name && nlen == 4 && memcmp(name, "set!", 4) == 0) {
                uint32_t klen = 0;
                const char *k = ts_query_string_value_for_id(
                    q, steps[i + 1].value_id, &klen);
                if (k && klen == key_len && memcmp(k, key, key_len) == 0) {
                    uint32_t vlen = 0;
                    const char *v = ts_query_string_value_for_id(
                        q, steps[i + 2].value_id, &vlen);
                    if (v && vlen + 1 < out_sz) {
                        memcpy(out, v, vlen);
                        out[vlen] = '\0';
                        return 1;
                    }
                }
            }
        }
        i = j + 1;
    }
    return 0;
}

static TSPoint byte_to_point(const char *src, uint32_t byte) {
    TSPoint p = {0, 0};
    for (uint32_t i = 0; i < byte; i++) {
        if (src[i] == '\n') {
            p.row++;
            p.column = 0;
        } else {
            p.column++;
        }
    }
    return p;
}

/* ===================================================================
 * Sub-language cache.
 * =================================================================== */
static TSSubLang *find_sub_lang(TSState *st, const char *lang_name) {
    for (int i = 0; i < st->num_sub_langs; i++)
        if (strcmp(st->sub_langs[i].lang_name, lang_name) == 0)
            return &st->sub_langs[i];
    return NULL;
}

static TSSubLang *get_or_create_sub_lang(TSState *st, const char *lang_name) {
    TSSubLang *s = find_sub_lang(st, lang_name);
    if (s) {
        if (s->load_failed)
            return NULL;
        return s;
    }
    if (st->num_sub_langs == st->cap_sub_langs) {
        int new_cap = st->cap_sub_langs ? st->cap_sub_langs * 2 : 4;
        TSSubLang *grown =
            realloc(st->sub_langs, (size_t)new_cap * sizeof(TSSubLang));
        if (!grown)
            return NULL;
        memset(grown + st->cap_sub_langs, 0,
               (size_t)(new_cap - st->cap_sub_langs) * sizeof(TSSubLang));
        st->sub_langs = grown;
        st->cap_sub_langs = new_cap;
    }
    s = &st->sub_langs[st->num_sub_langs++];
    memset(s, 0, sizeof(*s));
    strncpy(s->lang_name, lang_name, sizeof(s->lang_name) - 1);

    if (!load_lang_dl(lang_name, &s->lang, &s->dl_handle)) {
        log_msg("TS: failed to load sub-language '%s'", lang_name);
        s->load_failed = 1;
        return NULL;
    }
    s->parser = ts_parser_new();
    if (!ts_parser_set_language(s->parser, s->lang)) {
        log_msg("TS: parser_set_language failed for sub '%s'", lang_name);
        ts_parser_delete(s->parser);
        s->parser = NULL;
        if (s->dl_handle) {
            dlclose(s->dl_handle);
            s->dl_handle = NULL;
        }
        s->lang = NULL;
        s->load_failed = 1;
        return NULL;
    }
    s->query = load_lang_query(s->lang, lang_name, "highlights.scm");
    log_msg("TS: loaded sub-language '%s'%s", lang_name,
            s->query ? "" : " (no highlights)");
    return s;
}

/* ===================================================================
 * Injection collection.
 * =================================================================== */
static void add_injection(TSState *st, const char *lang_name, uint32_t s,
                          uint32_t e) {
    if (s >= e || !lang_name || !*lang_name)
        return;
    if (st->num_injections == st->cap_injections) {
        int new_cap = st->cap_injections ? st->cap_injections * 2 : 8;
        TSInjectionRange *grown = realloc(
            st->injections, (size_t)new_cap * sizeof(TSInjectionRange));
        if (!grown)
            return;
        st->injections = grown;
        st->cap_injections = new_cap;
    }
    TSInjectionRange *ir = &st->injections[st->num_injections++];
    memset(ir, 0, sizeof(*ir));
    strncpy(ir->lang_name, lang_name, sizeof(ir->lang_name) - 1);
    ir->start_byte = s;
    ir->end_byte = e;
}

static void collect_injections(TSState *st, const char *src, size_t src_len) {
    st->num_injections = 0;
    if (!st->inject_query || !st->tree)
        return;
    TSNode root = ts_tree_root_node(st->tree);
    TSQueryCursor *cur = ts_query_cursor_new();
    ts_query_cursor_exec(cur, st->inject_query, root);

    TSQueryMatch m;
    while (ts_query_cursor_next_match(cur, &m)) {
        TSNode content_node = {0};
        int has_content = 0;
        const char *dyn_lang_ptr = NULL;
        uint32_t dyn_lang_len = 0;

        for (uint32_t i = 0; i < m.capture_count; i++) {
            TSQueryCapture c = m.captures[i];
            const char *cname;
            uint32_t clen;
            cname =
                ts_query_capture_name_for_id(st->inject_query, c.index, &clen);
            if (!cname)
                continue;
            if (clen == 17 && memcmp(cname, "injection.content", 17) == 0) {
                content_node = c.node;
                has_content = 1;
            } else if (clen == 18 &&
                       memcmp(cname, "injection.language", 18) == 0) {
                uint32_t s = ts_node_start_byte(c.node);
                uint32_t e = ts_node_end_byte(c.node);
                if (e > s && (size_t)e <= src_len) {
                    dyn_lang_ptr = src + s;
                    dyn_lang_len = e - s;
                }
            }
        }
        if (!has_content)
            continue;

        char lang_buf[32] = {0};
        /* Static `(#set! injection.language "X")` takes precedence. */
        if (!find_set_string_value(st->inject_query, m.pattern_index,
                                   "injection.language", lang_buf,
                                   sizeof(lang_buf))) {
            if (dyn_lang_ptr && dyn_lang_len > 0 &&
                dyn_lang_len < sizeof(lang_buf)) {
                memcpy(lang_buf, dyn_lang_ptr, dyn_lang_len);
                lang_buf[dyn_lang_len] = '\0';
            } else {
                continue;
            }
        }

        uint32_t cs = ts_node_start_byte(content_node);
        uint32_t ce = ts_node_end_byte(content_node);
        add_injection(st, lang_buf, cs, ce);
    }
    ts_query_cursor_delete(cur);
}

/* ===================================================================
 * Sub-language reparse: feed each sub-parser its accumulated ranges.
 * =================================================================== */
static void reparse_sub_langs(TSState *st, const char *src, size_t src_len) {
    /* Distinct languages used this round (cap protects stack). */
    enum { MAX_DISTINCT = 16 };
    char langs[MAX_DISTINCT][32];
    int  nlangs = 0;
    for (int i = 0; i < st->num_injections && nlangs < MAX_DISTINCT; i++) {
        const char *ln = st->injections[i].lang_name;
        int found = 0;
        for (int j = 0; j < nlangs; j++)
            if (strcmp(langs[j], ln) == 0) {
                found = 1;
                break;
            }
        if (!found) {
            strncpy(langs[nlangs], ln, 31);
            langs[nlangs][31] = '\0';
            nlangs++;
        }
    }

    for (int li = 0; li < nlangs; li++) {
        TSSubLang *sub = get_or_create_sub_lang(st, langs[li]);
        if (!sub || !sub->parser)
            continue;

        enum { MAX_RANGES = 256 };
        TSRange ranges[MAX_RANGES];
        int rc = 0;
        for (int i = 0; i < st->num_injections && rc < MAX_RANGES; i++) {
            if (strcmp(st->injections[i].lang_name, langs[li]) != 0)
                continue;
            uint32_t s = st->injections[i].start_byte;
            uint32_t e = st->injections[i].end_byte;
            ranges[rc].start_byte = s;
            ranges[rc].end_byte = e;
            ranges[rc].start_point = byte_to_point(src, s);
            ranges[rc].end_point = byte_to_point(src, e);
            rc++;
        }
        if (rc == 0)
            continue;
        ts_parser_set_included_ranges(sub->parser, ranges, (uint32_t)rc);
        if (sub->tree) {
            ts_tree_delete(sub->tree);
            sub->tree = NULL;
        }
        sub->tree =
            ts_parser_parse_string(sub->parser, NULL, src, (uint32_t)src_len);
    }

    /* Drop trees for languages that no longer have any injection so we
     * don't render stale highlights. The lang/parser stay cached. */
    for (int i = 0; i < st->num_sub_langs; i++) {
        int still_used = 0;
        for (int j = 0; j < nlangs; j++)
            if (strcmp(st->sub_langs[i].lang_name, langs[j]) == 0) {
                still_used = 1;
                break;
            }
        if (!still_used && st->sub_langs[i].tree) {
            ts_tree_delete(st->sub_langs[i].tree);
            st->sub_langs[i].tree = NULL;
        }
    }
}

void ts_buffer_reparse(Buffer *buf) {
    if (!buf || !buf->ts_internal)
        return;
    TSState *st = (TSState *)buf->ts_internal;
    if (!st->parser || !st->lang)
        return;
    if (st->parsed_dirty == buf->dirty && st->tree)
        return;
    char *src = NULL;
    size_t len = build_source(buf, &src);
    if (!src)
        return;

    /* Make sure the host parser is unrestricted in case it was reused with
     * included_ranges set elsewhere. */
    ts_parser_set_included_ranges(st->parser, NULL, 0);

    if (st->tree)
        ts_tree_delete(st->tree);
    st->tree = ts_parser_parse_string(st->parser, NULL, src, (uint32_t)len);
    st->parsed_dirty = buf->dirty;

    collect_injections(st, src, len);
    reparse_sub_langs(st, src, len);

    free(src);
}

/* ===================================================================
 * Capture name → SGR.
 * =================================================================== */
static const char *capture_name_to_sgr(const char *name, uint32_t nlen) {
    if (nlen >= 6 && strncmp(name, "string", 6) == 0)
        return COLOR_STRING;
    if (nlen >= 6 && strncmp(name, "escape", 6) == 0)
        return COLOR_STRING;
    if (nlen >= 7 && strncmp(name, "comment", 7) == 0)
        return COLOR_COMMENT;
    if (nlen >= 8 && strncmp(name, "variable", 8) == 0)
        return COLOR_VARIABLE;
    if (nlen >= 8 && strncmp(name, "constant", 8) == 0)
        return COLOR_CONSTANT;
    if (nlen >= 6 && strncmp(name, "number", 6) == 0)
        return COLOR_NUMBER;
    if ((nlen >= 7 && strncmp(name, "keyword", 7) == 0) ||
        (nlen >= 11 && strncmp(name, "conditional", 11) == 0) ||
        (nlen >= 6 && strncmp(name, "repeat", 6) == 0) ||
        (nlen >= 7 && strncmp(name, "include", 7) == 0))
        return COLOR_KEYWORD;
    if ((nlen >= 4 && strncmp(name, "type", 4) == 0) ||
        (nlen >= 6 && strncmp(name, "module", 6) == 0) ||
        (nlen >= 11 && strncmp(name, "constructor", 11) == 0))
        return COLOR_TYPE;
    if (nlen >= 8 && strncmp(name, "function", 8) == 0)
        return COLOR_FUNCTION;
    if ((nlen >= 8 && strncmp(name, "property", 8) == 0) ||
        (nlen >= 9 && strncmp(name, "attribute", 9) == 0))
        return COLOR_ATTRIBUTE;
    if (nlen >= 5 && strncmp(name, "label", 5) == 0)
        return COLOR_LABEL;
    if (nlen >= 8 && strncmp(name, "operator", 8) == 0)
        return COLOR_OPERATOR;
    if ((nlen >= 11 && strncmp(name, "punctuation", 11) == 0) ||
        (nlen >= 9 && strncmp(name, "delimiter", 9) == 0))
        return COLOR_PUNCT;
    if (nlen >= 4 && strncmp(name, "text", 4) == 0) {
        if (nlen >= 10 && strncmp(name, "text.title", 10) == 0)
            return COLOR_TITLE;
        if (nlen >= 12 && strncmp(name, "text.literal", 12) == 0)
            return COLOR_STRING;
        if (nlen >= 8 && strncmp(name, "text.uri", 8) == 0)
            return COLOR_URI;
        if (nlen >= 14 && strncmp(name, "text.reference", 14) == 0)
            return COLOR_TYPE;
        if (nlen >= 11 && strncmp(name, "text.danger", 11) == 0)
            return COLOR_DIAG_ERROR;
        if (nlen >= 12 && strncmp(name, "text.warning", 12) == 0)
            return COLOR_DIAG_WARN;
        if (nlen >= 9 && strncmp(name, "text.note", 9) == 0)
            return COLOR_DIAG_NOTE;
        return COLOR_COMMENT;
    }
    if (nlen >= 9 && strncmp(name, "exception", 9) == 0)
        return COLOR_DIAG_ERROR;
    return NULL;
}

/* ===================================================================
 * Segment collection from a (tree, query) over a byte range.
 * =================================================================== */
typedef struct {
    uint32_t s, e;
    const char *sgr;
} Seg;

/* Closes a syntax segment without disturbing reverse video, so visual
 * selection (rendered with ESC[7m) survives across highlight boundaries.
 *   22 = normal intensity (cancels bold from COLOR_TITLE)
 *   24 = no underline    (cancels underline from COLOR_URI)
 *   39 = default foreground
 * Reverse video (7/27) is intentionally untouched. */
#define TS_SEG_RESET "\x1b[22;24;39m"

static int collect_segments(TSTree *tree, TSQuery *query, uint32_t cur_start,
                            uint32_t cur_end, uint32_t clip_start,
                            uint32_t clip_end, Seg *segs, int seg_cap,
                            int seg_count) {
    if (!tree || !query)
        return seg_count;
    TSNode root = ts_tree_root_node(tree);
    TSQueryCursor *cur = ts_query_cursor_new();
    ts_query_cursor_exec(cur, query, root);
    ts_query_cursor_set_byte_range(cur, cur_start, cur_end);

    TSQueryMatch m;
    while (ts_query_cursor_next_match(cur, &m) && seg_count < seg_cap) {
        for (uint32_t i = 0; i < m.capture_count && seg_count < seg_cap; i++) {
            TSQueryCapture c = m.captures[i];
            const char *name;
            uint32_t nlen;
            name = ts_query_capture_name_for_id(query, c.index, &nlen);
            if (!name)
                continue;
            const char *sgr = capture_name_to_sgr(name, nlen);
            if (!sgr)
                continue;
            uint32_t s = ts_node_start_byte(c.node);
            uint32_t e = ts_node_end_byte(c.node);
            if (e <= clip_start || s >= clip_end)
                continue;
            if (s < clip_start)
                s = clip_start;
            if (e > clip_end)
                e = clip_end;
            segs[seg_count++] = (Seg){s, e, sgr};
        }
    }
    ts_query_cursor_delete(cur);
    return seg_count;
}

size_t ts_highlight_line(Buffer *buf, int line_index, char *dst, size_t dst_cap,
                         int col_offset, int max_cols) {
    if (!g_ts_enabled || !buf || !buf->ts_internal)
        return 0;
    TSState *st = (TSState *)buf->ts_internal;
    if (!st->tree || !st->query)
        return 0;

    /* Compute byte range for this line in the source. */
    size_t start = 0;
    for (int i = 0; i < line_index; i++)
        start += buf->rows[i].chars.len + 1;
    size_t end = start + buf->rows[line_index].chars.len;

    enum { SEG_CAP = 256 };
    Seg segs[SEG_CAP];
    int sc = 0;

    /* Host segments. */
    sc = collect_segments(st->tree, st->query, (uint32_t)start, (uint32_t)end,
                          (uint32_t)start, (uint32_t)end, segs, SEG_CAP, sc);

    /* Strip host segments wholly inside an injection range — the
     * sub-language gets to colour those bytes. */
    if (st->num_injections > 0) {
        int kept = 0;
        for (int i = 0; i < sc; i++) {
            int inside = 0;
            for (int j = 0; j < st->num_injections; j++) {
                TSInjectionRange *ir = &st->injections[j];
                if (segs[i].s >= ir->start_byte &&
                    segs[i].e <= ir->end_byte) {
                    inside = 1;
                    break;
                }
            }
            if (!inside)
                segs[kept++] = segs[i];
        }
        sc = kept;
    }

    /* Sub-language segments for any injection that touches this line. */
    for (int j = 0; j < st->num_injections && sc < SEG_CAP; j++) {
        TSInjectionRange *ir = &st->injections[j];
        if (ir->end_byte <= start || ir->start_byte >= end)
            continue;
        TSSubLang *sub = find_sub_lang(st, ir->lang_name);
        if (!sub || !sub->tree || !sub->query)
            continue;
        uint32_t cs = (uint32_t)((ir->start_byte > start) ? ir->start_byte
                                                          : (uint32_t)start);
        uint32_t ce = (uint32_t)((ir->end_byte < end) ? ir->end_byte
                                                      : (uint32_t)end);
        sc = collect_segments(sub->tree, sub->query, cs, ce, cs, ce, segs,
                              SEG_CAP, sc);
    }

    /* Sort segments by start byte. */
    for (int i = 0; i < sc; i++)
        for (int j = i + 1; j < sc; j++)
            if (segs[j].s < segs[i].s) {
                Seg t = segs[i];
                segs[i] = segs[j];
                segs[j] = t;
            }

    /* Render the line.
     *
     * Tree-sitter segments use chars-space byte offsets (matching chars.data).
     * The display uses render.data where tabs are expanded to spaces.
     * We walk chars.data for segment comparison and render.data for output,
     * keeping both in sync so tab expansion doesn't shift highlight boundaries.
     */
    const Row  *row   = &buf->rows[line_index];
    const char *rdata = row->render.data;
    int         rlen  = (int)row->render.len;
    const char *cdata = row->chars.data;
    int         clen  = (int)row->chars.len;

    int roff = 0;
    int ci = 0;
    while (ci < clen && roff < col_offset) {
        unsigned char c = (unsigned char)cdata[ci];
        int w = (c == '\t') ? (TAB_STOP - roff % TAB_STOP) : 1;
        if (roff + w > col_offset)
            break;
        roff += w;
        ci++;
    }
    int rout = col_offset;

    int    si = 0;
    int    rem = max_cols;
    size_t out = 0;
    uint32_t cpos = (uint32_t)start + (uint32_t)ci;

    while (rem > 0 && ci < clen && rout < rlen) {
        while (si < sc && segs[si].e <= cpos)
            si++;

        unsigned char c = (unsigned char)cdata[ci];
        int rw = (c == '\t') ? (TAB_STOP - rout % TAB_STOP) : 1;
        if (rw < 1)
            rw = 1;

        int in_seg = (si < sc && segs[si].s <= cpos && cpos < segs[si].e);

        if (in_seg) {
            size_t el = strlen(segs[si].sgr);
            if (out + el < dst_cap) {
                memcpy(dst + out, segs[si].sgr, el);
                out += el;
            }
        }
        for (int k = 0; k < rw && rout < rlen && rem > 0; k++) {
            if (out + 1 < dst_cap)
                dst[out++] = rdata[rout];
            rout++;
            rem--;
        }
        if (in_seg) {
            size_t rl = strlen(TS_SEG_RESET);
            if (out + rl < dst_cap) {
                memcpy(dst + out, TS_SEG_RESET, rl);
                out += rl;
            }
        }

        cpos++;
        ci++;
    }
    return out;
}
