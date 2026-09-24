#include "hed.h"
#include "highlight.h"
#include "theme.h"
#include "ts.h"
#include <dlfcn.h>
#include <limits.h>
#include <regex.h>
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
    char lang_name[32];
    uint32_t start_byte;
    uint32_t end_byte;
} TSInjectionRange;

typedef struct {
    char lang_name[32];
    TSLanguage *lang;
    TSParser *parser;
    TSTree *tree;
    TSQuery *query;
    void *dl_handle;
    int load_failed; /* 1 once we know this lang can't be loaded */
} TSSubLang;

typedef struct {
    TSParser *parser;
    TSTree *tree;
    TSLanguage *lang;
    TSQuery *query;
    TSQuery *inject_query;
    void *dl_handle;
    char lang_name[32];
    /* Change detection. No single counter sees every edit (special
     * buffers rewrite rows and restore dirty; save and reload zero it;
     * undo replay doesn't bump the modification generation), so a
     * change of any of these reparses — and a false alarm costs a join
     * plus a compare, since the reparse diffs against src first. */
    int needs_parse; /* 1 = full parse (no old tree) on next render */
    unsigned long parsed_gen;
    int parsed_dirty;
    int parsed_rows;
    char *src; /* text the trees were parsed from (node byte
                  offsets index into it); predicates read it */
    size_t src_len;
    uint32_t *line_starts; /* byte offset of each line of src */
    int num_lines;

    TSInjectionRange *injections;
    int num_injections;
    int cap_injections;

    TSSubLang *sub_langs;
    int num_sub_langs;
    int cap_sub_langs;
} TSState;

void ts_set_enabled(int on) { g_ts_enabled = on ? 1 : 0; }
int ts_is_enabled(void) { return g_ts_enabled; }

/* ===================================================================
 * Per-buffer state, plugin-owned. A small stb_ds vector keyed by
 * Buffer* so core doesn't carry a tree-sitter-shaped slot on Buffer.
 *
 * Linear scan is fine here: a typical session has < 100 buffers and
 * lookups happen O(once per frame per visible buffer). Using stb_ds's
 * hash map (hmput / hmgeti / hmdel) would need typeof, which our
 * `-std=c11 -pedantic` build rejects.
 * =================================================================== */
typedef struct {
    Buffer *key;
    TSState *value;
} TSStateEntry;
static TSStateEntry *g_states = NULL;

static int ts_state_index(Buffer *buf) {
    int n = (int)arrlen(g_states);
    for (int i = 0; i < n; i++)
        if (g_states[i].key == buf)
            return i;
    return -1;
}

static TSState *ts_state_get(Buffer *buf) {
    if (!buf)
        return NULL;
    int i = ts_state_index(buf);
    return i < 0 ? NULL : g_states[i].value;
}

/* Lazily allocate a TSState for `buf`. Idempotent: returns the
 * existing state when one is already attached. */
static TSState *ts_state_create(Buffer *buf) {
    if (!buf)
        return NULL;
    TSState *st = ts_state_get(buf);
    if (st)
        return st;
    st = calloc(1, sizeof(TSState));
    if (!st)
        return NULL;
    st->needs_parse = 1;
    TSStateEntry e = {.key = buf, .value = st};
    arrput(g_states, e);
    return st;
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

static void ts_state_destroy(Buffer *buf) {
    if (!buf)
        return;
    int i = ts_state_index(buf);
    if (i < 0)
        return;
    TSState *st = g_states[i].value;
    if (st) {
        if (st->tree)
            ts_tree_delete(st->tree);
        free(st->src);
        free(st->line_starts);
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
            for (int j = 0; j < st->num_sub_langs; j++)
                free_sub_lang(&st->sub_langs[j]);
            free(st->sub_langs);
        }
        free(st);
    }
    arrdel(g_states, i);
}

/* Public hook handlers — registered by treesitter_init. */
void ts_on_buffer_open(HookBufferEvent *e) {
    if (!e || !e->buf)
        return;
    if (!g_ts_enabled)
        return;
    if (ts_buffer_autoload(e->buf))
        ts_buffer_reparse(e->buf);
}

void ts_on_buffer_close(HookBufferEvent *e) {
    if (!e || !e->buf)
        return;
    ts_state_destroy(e->buf);
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

/* List the installed grammar names (the *.so basenames in the grammar
 * dir), sorted. *out_names is a malloc'd array of malloc'd strings;
 * caller frees both. Returns the count (0 when none / no dir). */
int ts_list_langs(char ***out_names) {
    *out_names = NULL;
    char base[PATH_MAX];
    ts_default_base(base, sizeof(base));
    if (!base[0])
        snprintf(base, sizeof(base), "ts");

    FsDir *d = NULL;
    if (fs_dir_open(&d, base) != ED_OK)
        return 0;

    char **names = NULL;
    int n = 0, cap = 0;
    FsDirEntry de;
    while (fs_dir_next(d, &de)) {
        size_t len = strlen(de.name);
        if (de.is_dir || len <= 3 || strcmp(de.name + len - 3, ".so") != 0)
            continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 16;
            char **nn = realloc(names, (size_t)cap * sizeof(*nn));
            if (!nn)
                break;
            names = nn;
        }
        char *name = malloc(len - 2);
        if (!name)
            continue;
        memcpy(name, de.name, len - 3);
        name[len - 3] = '\0';
        names[n++] = name;
    }
    fs_dir_close(d);

    for (int i = 1; i < n; i++) /* insertion sort: n is tiny */
        for (int j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; j--) {
            char *t = names[j];
            names[j] = names[j - 1];
            names[j - 1] = t;
        }
    *out_names = names;
    return n;
}

/* Load a tree-sitter language .so and return its TSLanguage* and dl handle. */
/* What people write in a fence info string, or in :tslang, against
 * the grammar that actually implements it. Injections carry these
 * names straight from the document (```sh, ```py), so without the
 * table a fenced block stays unhighlighted for want of an sh.so. */
static const char *lang_alias(const char *name) {
    static const char *const map[][2] = {
        {"sh", "bash"},       {"shell", "bash"},     {"zsh", "bash"},
        {"console", "bash"},  {"js", "javascript"},  {"jsx", "javascript"},
        {"ts", "typescript"}, {"tsx", "typescript"}, {"py", "python"},
        {"rb", "ruby"},       {"yml", "yaml"},       {"md", "markdown"},
        {"cs", "c-sharp"},    {"csharp", "c-sharp"}, {"c++", "cpp"},
        {"h", "c"},           {"hpp", "cpp"},        {"golang", "go"},
        {"htm", "html"},      {"conf", "ini"},
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (strcmp(name, map[i][0]) == 0)
            return map[i][1];
    return name;
}

static int load_lang_dl(const char *lang_name, TSLanguage **out_lang,
                        void **out_handle) {
    if (!lang_name || !*lang_name)
        return 0;
    char path[PATH_MAX];
    char base[PATH_MAX];
    ts_default_base(base, sizeof(base));
    /* Truncation harmless: an over-long path just fails to dlopen below. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    if (base[0])
        snprintf(path, sizeof(path), "%s/%s.so", base, lang_name);
    else
        snprintf(path, sizeof(path), "ts/%s.so", lang_name);
#pragma GCC diagnostic pop
    void *h = dlopen(path, RTLD_NOW);
    if (!h) {
        log_msg("TS dlopen failed for lang %s: %s", lang_name, dlerror());
        return 0;
    }

    /* Grammar names may carry '-' (c-sharp); the exported symbol
     * uses '_' (tree_sitter_c_sharp). */
    char sym[64];
    snprintf(sym, sizeof(sym), "tree_sitter_%s", lang_name);
    for (char *c = sym; *c; c++)
        if (*c == '-')
            *c = '_';
    /* dlsym returns void*; converting to a function pointer is POSIX-blessed
     * but ISO C forbids it. The cast is required by the ABI we're calling. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
    TSLanguage *(*langfn)(void) = (TSLanguage * (*)(void)) dlsym(h, sym);
#pragma GCC diagnostic pop
    if (!langfn) {
        log_msg("TS dlsym failed for lang %s: no %s in %s", lang_name, sym,
                path);
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
    char *buf = NULL;
    size_t sz = 0;
    if (fs_file_read(qpath, &buf, &sz) != ED_OK)
        return NULL;
    uint32_t err_offset;
    TSQueryError err_type;
    TSQuery *q = ts_query_new(lang, buf, (uint32_t)sz, &err_offset, &err_type);
    if (!q) {
        log_msg("TS query parse error in %s at offset %u (err=%d)", qpath,
                err_offset, (int)err_type);
    }
    free(buf);
    return q;
}

/* Plugin-registered query strings. Keyed by "<lang>/<qname>"; values
 * are borrowed pointers to caller-owned static strings. */
typedef struct {
    char *key;
    const char *value;
} QEntry;
static QEntry *g_query_registry = NULL;
static int g_query_registry_inited = 0;

static void make_qkey(char *out, size_t out_sz, const char *lang,
                      const char *qname) {
    snprintf(out, out_sz, "%s/%s", lang, qname);
}

int ts_register_query(const char *lang_name, const char *qname,
                      const char *content) {
    if (!lang_name || !qname || !content)
        return -1;
    if (!g_query_registry_inited) {
        sh_new_strdup(g_query_registry);
        g_query_registry_inited = 1;
    }
    char key[128];
    make_qkey(key, sizeof(key), lang_name, qname);
    shput(g_query_registry, key, content);
    return 0;
}

static const char *find_registered_query(const char *lang_name,
                                         const char *qname) {
    if (!g_query_registry_inited)
        return NULL;
    char key[128];
    make_qkey(key, sizeof(key), lang_name, qname);
    QEntry *e = shgetp_null(g_query_registry, key);
    return e ? e->value : NULL;
}

static TSQuery *parse_query_string(TSLanguage *lang, const char *src,
                                   const char *origin) {
    if (!lang || !src)
        return NULL;
    uint32_t err_offset;
    TSQueryError err_type;
    TSQuery *q =
        ts_query_new(lang, src, (uint32_t)strlen(src), &err_offset, &err_type);
    if (!q) {
        log_msg("TS query parse error in %s at offset %u (err=%d)",
                origin ? origin : "<embedded>", err_offset, (int)err_type);
    }
    return q;
}

/* Lookup order:
 *   1. <base>/queries.local/<lang>/<qname>   — user override (handwritten)
 *   2. plugin-registered embedded string      — enhanced defaults shipped
 *                                               by a plugin (e.g. markdown)
 *   3. HED_SRC_DIR/queries/<lang>/<qname>     — enhanced defaults shipped
 *                                               in-tree (e.g. c-sharp)
 *   4. <base>/queries/<lang>/<qname>          — tsi-installed defaults from
 *                                               the upstream grammar
 *   5. ./queries/<lang>/<qname>               — cwd-local (development)
 *
 * Rationale: tsi-installed XDG queries are just bundled upstream defaults
 * and shouldn't beat enhanced defaults shipped at the editor level (by a
 * plugin or in-tree). Users who want to override those drop their file
 * in <base>/queries.local/, which wins over everything. */
static TSQuery *load_lang_query(TSLanguage *lang, const char *lang_name,
                                const char *qname) {
    char base[PATH_MAX];
    ts_default_base(base, sizeof(base));
    char qpath[PATH_MAX];

    /* 1. user override */
    if (base[0]) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        snprintf(qpath, sizeof(qpath), "%s/queries.local/%s/%s", base,
                 lang_name, qname);
#pragma GCC diagnostic pop
        TSQuery *q = load_query_file(lang, qpath);
        if (q)
            return q;
    }

    /* 2. plugin-registered embedded string */
    const char *embedded = find_registered_query(lang_name, qname);
    if (embedded) {
        char origin[64];
        snprintf(origin, sizeof(origin), "embedded:%s/%s", lang_name, qname);
        TSQuery *q = parse_query_string(lang, embedded, origin);
        if (q)
            return q;
    }

    /* 3. in-tree enhanced defaults */
#ifdef HED_SRC_DIR
    {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        snprintf(qpath, sizeof(qpath), HED_SRC_DIR "/queries/%s/%s", lang_name,
                 qname);
#pragma GCC diagnostic pop
        TSQuery *q = load_query_file(lang, qpath);
        if (q)
            return q;
    }
#endif

    /* 4. tsi-installed defaults */
    if (base[0]) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        snprintf(qpath, sizeof(qpath), "%s/queries/%s/%s", base, lang_name,
                 qname);
#pragma GCC diagnostic pop
        TSQuery *q = load_query_file(lang, qpath);
        if (q)
            return q;
    }

    /* 5. cwd-local */
    snprintf(qpath, sizeof(qpath), "queries/%s/%s", lang_name, qname);
    return load_query_file(lang, qpath);
}

static int ts_lang_is_loaded(TSState *st, const char *lang_name) {
    if (!st || !st->lang || !st->parser)
        return 0;
    return strcmp(st->lang_name, lang_name) == 0;
}

int ts_buffer_load_language(Buffer *buf, const char *lang_name) {
    lang_name = lang_alias(lang_name);
    if (!buf)
        return 0;
    TSState *st = ts_state_create(buf);
    if (!st)
        return 0;

    if (ts_lang_is_loaded(st, lang_name)) {
        st->needs_parse = 1;
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
    free(st->src);
    st->src = NULL;
    st->src_len = 0;
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
    st->needs_parse = 1;

    /* The previous host's sub-language assumptions don't carry over. */
    if (st->sub_langs) {
        for (int i = 0; i < st->num_sub_langs; i++)
            free_sub_lang(&st->sub_langs[i]);
        st->num_sub_langs = 0;
    }
    st->num_injections = 0;

    if (!load_lang_dl(lang_name, &st->lang, &st->dl_handle))
        return 0;
    safe_strcpy(st->lang_name, lang_name, sizeof(st->lang_name));
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

/* Filetype string (as fs_path_detect_filetype yields) → tree-sitter
 * grammar name. Falls back to the filetype verbatim, which is right
 * for most grammars (zig, go, lua, ...). */
static const char *ts_grammar_for_filetype(const char *ft) {
    if (strcmp(ft, "csharp") == 0)
        return "c-sharp";
    if (strcmp(ft, "shell") == 0)
        return "bash";
    if (strcmp(ft, "Makefile") == 0)
        return "make";
    return ft;
}

/* Inverse of ts_grammar_for_filetype: grammar name → the filetype
 * string the rest of the editor keys on (filetype keybinds, folds,
 * :fmt, smart_indent). */
const char *ts_filetype_for_grammar(const char *lang) {
    if (strcmp(lang, "c-sharp") == 0)
        return "csharp";
    if (strcmp(lang, "bash") == 0)
        return "shell";
    if (strcmp(lang, "make") == 0)
        return "Makefile";
    return lang;
}

int ts_buffer_autoload(Buffer *buf) {
    if (!buf || !buf->filename)
        return 0;
    TSState *st = ts_state_create(buf);
    if (!st)
        return 0;
    const char *want = NULL;

    /* User filetype overrides (config fs_filetype_register / :ftmap)
     * beat the built-in extension table, so remapping e.g. ".h" to
     * "cpp" switches the grammar too. */
    const char *user_ft = fs_filetype_registered(buf->filename);
    if (user_ft)
        want = ts_grammar_for_filetype(user_ft);

    /* Detect by extension. NOTE: the map targets tree-sitter grammar
     * names (c-sharp, commonlisp, typescript, …), which intentionally
     * differ from the filetype strings fs_path_detect_filetype yields —
     * so it stays a local table, but the extension itself comes from the
     * shared, basename-aware fs_path_extension. */
    const char *ext = fs_path_extension(buf->filename);
    if (!want && *ext) {
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
        else if (strcmp(ext, "toml") == 0 || strcmp(ext, "conf") == 0)
            want = "toml";
        else if (strcmp(ext, "diff") == 0 || strcmp(ext, "patch") == 0)
            want = "diff";
        else if (strcmp(ext, "ini") == 0)
            want = "ini";
        else if (strcmp(ext, "md") == 0 || strcmp(ext, "markdown") == 0)
            want = "markdown";
        else if (strcmp(ext, "xml") == 0 || strcmp(ext, "csproj") == 0 ||
                 strcmp(ext, "props") == 0 || strcmp(ext, "targets") == 0 ||
                 strcmp(ext, "nuspec") == 0 || strcmp(ext, "resx") == 0 ||
                 strcmp(ext, "xaml") == 0 || strcmp(ext, "svg") == 0 ||
                 strcmp(ext, "xsd") == 0 || strcmp(ext, "xsl") == 0 ||
                 strcmp(ext, "xslt") == 0 || strcmp(ext, "plist") == 0)
            want = "xml";
    }

    if (!want) {
        const char *base = fs_path_basename(buf->filename);
        if (strcmp(base, "makefile") == 0 || strcmp(base, "Makefile") == 0)
            want = "make";
    }

    /* Anything else: a grammar installed under the extension's own name
     * (`tsi zig` → zig.so for .zig) works without a table entry. */
    if (!want && *ext) {
        char base[PATH_MAX], path[PATH_MAX];
        ts_default_base(base, sizeof(base));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        snprintf(path, sizeof(path), "%s/%s.so", base[0] ? base : "ts", ext);
#pragma GCC diagnostic pop
        if (access(path, R_OK) == 0)
            want = ext;
    }

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
        while (j < step_count && steps[j].type != TSQueryPredicateStepTypeDone)
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

/* ===================================================================
 * Predicate evaluation: #eq? #not-eq? #match? #not-match? #any-of?
 *
 * tree-sitter's C API hands back every match regardless of predicates;
 * evaluating them is the caller's job. Directives (#set!) and unknown
 * predicates are ignored, i.e. the pattern matches.
 * =================================================================== */

typedef struct {
    char *key; /* regex source */
    regex_t *value;
} RegexEntry;

static RegexEntry *g_regex_cache = NULL; /* stb_ds string map */

/* Upstream queries are written for Rust's regex crate; POSIX ERE lacks
 * the \d \w \s classes, so rewrite those (bare inside a bracket
 * expression, bracketed outside). Everything else passes through. */
static void regex_to_ere(const char *in, size_t in_len, char *out,
                         size_t out_sz) {
    size_t o = 0;
    int in_bracket = 0;
    for (size_t i = 0; i < in_len && o + 16 < out_sz; i++) {
        const char *rep = NULL;
        if (in[i] == '\\' && i + 1 < in_len) {
            switch (in[i + 1]) {
            case 'd':
                rep = in_bracket ? "0-9" : "[0-9]";
                break;
            case 'w':
                rep = in_bracket ? "A-Za-z0-9_" : "[A-Za-z0-9_]";
                break;
            case 's':
                rep = in_bracket ? "[:space:]" : "[[:space:]]";
                break;
            default:
                /* keep the escape pair verbatim so a "\[" doesn't
                 * start a bracket expression below */
                out[o++] = in[i];
                out[o++] = in[i + 1];
                i++;
                continue;
            }
        }
        if (rep) {
            size_t rl = strlen(rep);
            memcpy(out + o, rep, rl);
            o += rl;
            i++;
            continue;
        }
        if (in[i] == '[' && !in_bracket)
            in_bracket = 1;
        else if (in[i] == ']' && in_bracket)
            in_bracket = 0;
        out[o++] = in[i];
    }
    out[o] = '\0';
}

static regex_t *regex_cached(const char *pat, uint32_t pat_len) {
    char key[256];
    if (pat_len >= sizeof(key))
        return NULL;
    memcpy(key, pat, pat_len);
    key[pat_len] = '\0';
    if (!g_regex_cache)
        sh_new_strdup(g_regex_cache);
    RegexEntry *e = shgetp_null(g_regex_cache, key);
    if (e)
        return e->value; /* NULL if it failed to compile once */
    char ere[512];
    regex_to_ere(pat, pat_len, ere, sizeof(ere));
    regex_t *re = malloc(sizeof(*re));
    if (re && regcomp(re, ere, REG_EXTENDED | REG_NOSUB) != 0) {
        log_msg("TS: bad #match? regex '%s'", key);
        free(re);
        re = NULL;
    }
    shput(g_regex_cache, key, re);
    return re;
}

/* Text of the first captured node with capture index `id`, or NULL. */
static const char *match_capture_text(const TSQueryMatch *m, uint32_t id,
                                      const char *src, size_t src_len,
                                      uint32_t *len_out) {
    for (uint32_t i = 0; i < m->capture_count; i++) {
        if (m->captures[i].index != id)
            continue;
        uint32_t s = ts_node_start_byte(m->captures[i].node);
        uint32_t e = ts_node_end_byte(m->captures[i].node);
        if (e > src_len || s > e)
            return NULL;
        *len_out = e - s;
        return src + s;
    }
    return NULL;
}

static int match_predicates_ok(const TSQuery *q, const TSQueryMatch *m,
                               const char *src, size_t src_len) {
    uint32_t step_count = 0;
    const TSQueryPredicateStep *steps =
        ts_query_predicates_for_pattern(q, m->pattern_index, &step_count);
    if (!steps || step_count == 0 || !src)
        return 1;

    uint32_t i = 0;
    while (i < step_count) {
        uint32_t j = i;
        while (j < step_count && steps[j].type != TSQueryPredicateStepTypeDone)
            j++;
        /* Predicate occupies [i, j): name, then arguments. */
        uint32_t nlen = 0;
        const char *name =
            steps[i].type == TSQueryPredicateStepTypeString
                ? ts_query_string_value_for_id(q, steps[i].value_id, &nlen)
                : NULL;
        int negate = name && nlen > 4 && memcmp(name, "not-", 4) == 0;
        const char *op = negate ? name + 4 : name;
        uint32_t olen = negate ? nlen - 4 : nlen;
        int is_eq = name && olen == 3 && memcmp(op, "eq?", 3) == 0;
        int is_match = name && olen == 6 && memcmp(op, "match?", 6) == 0;
        int is_any =
            name && !negate && olen == 7 && memcmp(op, "any-of?", 7) == 0;

        if ((is_eq || is_match || is_any) && j >= i + 3 &&
            steps[i + 1].type == TSQueryPredicateStepTypeCapture) {
            uint32_t tlen = 0;
            const char *text = match_capture_text(m, steps[i + 1].value_id, src,
                                                  src_len, &tlen);
            int ok = 0;
            if (text && is_match) {
                uint32_t plen = 0;
                const char *pat = ts_query_string_value_for_id(
                    q, steps[i + 2].value_id, &plen);
                regex_t *re = pat ? regex_cached(pat, plen) : NULL;
                if (re) {
                    char tmp[512];
                    if (tlen < sizeof(tmp)) {
                        memcpy(tmp, text, tlen);
                        tmp[tlen] = '\0';
                        ok = regexec(re, tmp, 0, NULL, 0) == 0;
                    }
                } else {
                    ok = !negate; /* unusable regex: don't filter */
                }
            } else if (text) {
                /* eq? against a string or a second capture; any-of?
                 * against each remaining string. */
                for (uint32_t k = i + 2; k < j && !ok; k++) {
                    uint32_t olen2 = 0;
                    const char *other =
                        steps[k].type == TSQueryPredicateStepTypeCapture
                            ? match_capture_text(m, steps[k].value_id, src,
                                                 src_len, &olen2)
                            : ts_query_string_value_for_id(q, steps[k].value_id,
                                                           &olen2);
                    ok = other && olen2 == tlen &&
                         memcmp(other, text, tlen) == 0;
                }
            }
            if (negate)
                ok = !ok;
            if (!ok)
                return 0;
        }
        i = j + 1;
    }
    return 1;
}

/* Line containing `byte`: the last line starting at or before it. */
static int line_of_byte(const uint32_t *line_starts, int num_lines,
                        uint32_t byte) {
    int lo = 0, hi = num_lines - 1, row = 0;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (line_starts[mid] <= byte) {
            row = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return row;
}

static TSPoint byte_to_point(const uint32_t *line_starts, int num_lines,
                             uint32_t byte) {
    int row = line_of_byte(line_starts, num_lines, byte);
    TSPoint p = {(uint32_t)row, byte - line_starts[row]};
    return p;
}

/* The buffer's text (rows joined by '\n', as buf_to_text) plus the
 * offset where each row begins — one pass over the rows instead of a
 * join and then a newline scan. The table has num_rows + 1 entries:
 * the text ends in '\n', so there is one empty last line. */
static char *join_rows(const Buffer *buf, size_t *out_len,
                       uint32_t **out_starts, int *out_n) {
    size_t total = 0;
    for (int r = 0; r < buf->num_rows; r++)
        total += buf->rows[r].chars.len + 1;
    char *out = malloc(total + 1);
    uint32_t *ls = malloc(((size_t)buf->num_rows + 1) * sizeof(uint32_t));
    if (!out || !ls) {
        free(out);
        free(ls);
        return NULL;
    }
    char *p = out;
    for (int r = 0; r < buf->num_rows; r++) {
        ls[r] = (uint32_t)(p - out);
        memcpy(p, buf->rows[r].chars.data, buf->rows[r].chars.len);
        p += buf->rows[r].chars.len;
        *p++ = '\n';
    }
    ls[buf->num_rows] = (uint32_t)total;
    *p = '\0';
    *out_len = total;
    *out_starts = ls;
    *out_n = buf->num_rows + 1;
    return out;
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
    safe_strcpy(s->lang_name, lang_name, sizeof(s->lang_name));

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
        TSInjectionRange *grown =
            realloc(st->injections, (size_t)new_cap * sizeof(TSInjectionRange));
        if (!grown)
            return;
        st->injections = grown;
        st->cap_injections = new_cap;
    }
    TSInjectionRange *ir = &st->injections[st->num_injections++];
    memset(ir, 0, sizeof(*ir));
    safe_strcpy(ir->lang_name, lang_name, sizeof(ir->lang_name));
    ir->start_byte = s;
    ir->end_byte = e;
}

/* Append the injections whose nodes intersect bytes [lo, hi) of the
 * tree. The injection query over a whole large document is slow (a
 * markdown file has one inline injection per paragraph), so after an
 * edit only the changed span is re-queried — see update_injections. */
static void collect_injections_in(TSState *st, const char *src, size_t src_len,
                                  uint32_t lo, uint32_t hi) {
    if (!st->inject_query || !st->tree)
        return;
    TSNode root = ts_tree_root_node(st->tree);
    TSQueryCursor *cur = ts_query_cursor_new();
    ts_query_cursor_exec(cur, st->inject_query, root);
    ts_query_cursor_set_byte_range(cur, lo, hi);

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
        /* Canonical from here on: the document says ```sh, the
         * grammar and its queries are bash's. Downstream keys — the
         * sub-language states and the "still used" sweep — must agree
         * on one spelling. */
        add_injection(st, lang_alias(lang_buf), cs, ce);
    }
    ts_query_cursor_delete(cur);
}

static void collect_injections(TSState *st, const char *src, size_t src_len) {
    st->num_injections = 0;
    collect_injections_in(st, src, src_len, 0, (uint32_t)src_len);
}

static int cmp_injection(const void *a, const void *b) {
    const TSInjectionRange *x = a, *y = b;
    return (x->start_byte > y->start_byte) - (x->start_byte < y->start_byte);
}

/* Bring st->injections up to date after an incremental parse. Those
 * clear of the edit keep their language and move with the text; those
 * touching it, or anything the parse reports as changed, are dropped
 * and that span re-queried. `old` is the edited previous tree. */
static void update_injections(TSState *st, const char *src, size_t src_len,
                              const TSInputEdit *e, TSTree *old) {
    if (!st->inject_query || !st->tree) {
        st->num_injections = 0;
        return;
    }
    uint32_t lo = e->start_byte, hi = e->new_end_byte;
    uint32_t nch = 0;
    TSRange *ch = ts_tree_get_changed_ranges(old, st->tree, &nch);
    for (uint32_t i = 0; i < nch; i++) {
        if (ch[i].start_byte < lo)
            lo = ch[i].start_byte;
        if (ch[i].end_byte > hi)
            hi = ch[i].end_byte;
    }
    free(ch);

    /* To new-text offsets; a range overlapping the edit is dropped and
     * its extent joins the span to re-query. */
    int64_t delta = (int64_t)e->new_end_byte - (int64_t)e->old_end_byte;
    int k = 0;
    for (int i = 0; i < st->num_injections; i++) {
        TSInjectionRange ir = st->injections[i];
        if (ir.end_byte < e->start_byte) {
            /* before the edit: unchanged */
        } else if (ir.start_byte > e->old_end_byte) {
            ir.start_byte = (uint32_t)(ir.start_byte + delta);
            ir.end_byte = (uint32_t)(ir.end_byte + delta);
        } else {
            if (ir.start_byte < lo)
                lo = ir.start_byte;
            int64_t end = (int64_t)ir.end_byte + delta;
            if (end > (int64_t)hi)
                hi = (uint32_t)end;
            continue;
        }
        st->injections[k++] = ir;
    }
    st->num_injections = k;

    /* Drop everything touching the span (inclusive, so a range ending
     * right where the edit starts is re-derived too), widening it until
     * no kept range reaches in. */
    int changed = 1;
    while (changed) {
        changed = 0;
        k = 0;
        for (int i = 0; i < st->num_injections; i++) {
            TSInjectionRange ir = st->injections[i];
            if (ir.end_byte >= lo && ir.start_byte <= hi) {
                if (ir.start_byte < lo)
                    lo = ir.start_byte;
                if (ir.end_byte > hi)
                    hi = ir.end_byte;
                changed = 1;
                continue;
            }
            st->injections[k++] = ir;
        }
        st->num_injections = k;
    }

    uint32_t qhi = hi < src_len ? hi + 1 : (uint32_t)src_len;
    collect_injections_in(st, src, src_len, lo, qhi);
    qsort(st->injections, (size_t)st->num_injections, sizeof(TSInjectionRange),
          cmp_injection);
}

/* ===================================================================
 * Sub-language reparse: feed each sub-parser its accumulated ranges.
 * =================================================================== */
static void reparse_sub_langs(TSState *st, const char *src, size_t src_len,
                              const uint32_t *line_starts, int num_lines) {
    /* Distinct languages used this round (cap protects stack). */
    enum { MAX_DISTINCT = 16 };
    char langs[MAX_DISTINCT][32];
    int nlangs = 0;
    for (int i = 0; i < st->num_injections && nlangs < MAX_DISTINCT; i++) {
        const char *ln = st->injections[i].lang_name;
        int found = 0;
        for (int j = 0; j < nlangs; j++)
            if (strcmp(langs[j], ln) == 0) {
                found = 1;
                break;
            }
        if (!found) {
            safe_strcpy(langs[nlangs], ln, sizeof(langs[nlangs]));
            nlangs++;
        }
    }

    for (int li = 0; li < nlangs; li++) {
        TSSubLang *sub = get_or_create_sub_lang(st, langs[li]);
        if (!sub || !sub->parser)
            continue;

        TSRange *ranges = malloc((size_t)st->num_injections * sizeof(TSRange));
        if (!ranges)
            continue;
        int rc = 0;
        for (int i = 0; i < st->num_injections; i++) {
            if (strcmp(st->injections[i].lang_name, langs[li]) != 0)
                continue;
            uint32_t s = st->injections[i].start_byte;
            uint32_t e = st->injections[i].end_byte;
            ranges[rc].start_byte = s;
            ranges[rc].end_byte = e;
            ranges[rc].start_point = byte_to_point(line_starts, num_lines, s);
            ranges[rc].end_point = byte_to_point(line_starts, num_lines, e);
            rc++;
        }
        if (rc == 0) {
            free(ranges);
            continue;
        }
        ts_parser_set_included_ranges(sub->parser, ranges, (uint32_t)rc);
        free(ranges);
        /* sub->tree, when kept, was already ts_tree_edit'ed by the
         * caller; tree-sitter reuses it across changed ranges. */
        TSTree *old = sub->tree;
        sub->tree =
            ts_parser_parse_string(sub->parser, old, src, (uint32_t)src_len);
        if (old)
            ts_tree_delete(old);
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

static int ts_buffer_changed(const TSState *st, const Buffer *buf) {
    return st->needs_parse || !st->tree ||
           st->parsed_gen != undo_mod_generation() ||
           st->parsed_dirty != buf->dirty || st->parsed_rows != buf->num_rows;
}

/* Describe the change from st->src to src as one edit: everything
 * between the common prefix and the common suffix. Returns 0 when the
 * texts are identical. */
static int ts_diff_edit(const TSState *st, const char *src, size_t len,
                        const uint32_t *line_starts, int num_lines,
                        TSInputEdit *edit) {
    size_t old_len = st->src_len;
    size_t min = old_len < len ? old_len : len;
    /* Skip equal blocks with memcmp, then finish byte by byte. */
    enum { BLK = 4096 };
    size_t pre = 0;
    while (pre + BLK <= min && memcmp(st->src + pre, src + pre, BLK) == 0)
        pre += BLK;
    while (pre < min && st->src[pre] == src[pre])
        pre++;
    if (pre == min && old_len == len)
        return 0;
    size_t suf = 0;
    while (suf + BLK <= min - pre && memcmp(st->src + old_len - suf - BLK,
                                            src + len - suf - BLK, BLK) == 0)
        suf += BLK;
    while (suf < min - pre && st->src[old_len - 1 - suf] == src[len - 1 - suf])
        suf++;
    edit->start_byte = (uint32_t)pre;
    edit->old_end_byte = (uint32_t)(old_len - suf);
    edit->new_end_byte = (uint32_t)(len - suf);
    edit->start_point = byte_to_point(line_starts, num_lines, (uint32_t)pre);
    edit->old_end_point =
        byte_to_point(st->line_starts, st->num_lines, edit->old_end_byte);
    edit->new_end_point =
        byte_to_point(line_starts, num_lines, edit->new_end_byte);
    return 1;
}

void ts_buffer_reparse(Buffer *buf) {
    if (!buf)
        return;
    TSState *st = ts_state_get(buf);
    if (!st || !st->parser || !st->lang)
        return;
    if (!ts_buffer_changed(st, buf))
        return;
    st->parsed_gen = undo_mod_generation();
    st->parsed_dirty = buf->dirty;
    st->parsed_rows = buf->num_rows;

    size_t len = 0;
    int num_lines = 0;
    uint32_t *line_starts = NULL;
    char *src = join_rows(buf, &len, &line_starts, &num_lines);
    if (!src)
        return;

    /* Incremental: tell the old trees what changed and let tree-sitter
     * reuse everything outside the edit. A full parse only when forced
     * (language (re)load) or there is nothing to reuse. */
    TSTree *old = NULL;
    TSInputEdit edit;
    if (!st->needs_parse && st->tree && st->src) {
        if (!ts_diff_edit(st, src, len, line_starts, num_lines, &edit)) {
            free(src);
            free(line_starts);
            return; /* false alarm: same text */
        }
        ts_tree_edit(st->tree, &edit);
        for (int i = 0; i < st->num_sub_langs; i++)
            if (st->sub_langs[i].tree)
                ts_tree_edit(st->sub_langs[i].tree, &edit);
        old = st->tree;
    } else {
        if (st->tree)
            ts_tree_delete(st->tree);
        for (int i = 0; i < st->num_sub_langs; i++)
            if (st->sub_langs[i].tree) {
                ts_tree_delete(st->sub_langs[i].tree);
                st->sub_langs[i].tree = NULL;
            }
    }
    st->needs_parse = 0;

    /* Make sure the host parser is unrestricted in case it was reused with
     * included_ranges set elsewhere. */
    ts_parser_set_included_ranges(st->parser, NULL, 0);
    st->tree = ts_parser_parse_string(st->parser, old, src, (uint32_t)len);
    if (old && st->tree)
        update_injections(st, src, len, &edit, old);
    else
        collect_injections(st, src, len);
    if (old)
        ts_tree_delete(old);
    reparse_sub_langs(st, src, len, line_starts, num_lines);

    free(st->src);
    st->src = src;
    st->src_len = len;
    free(st->line_starts);
    st->line_starts = line_starts;
    st->num_lines = num_lines;
}

/* ===================================================================
 * Default palette + role mapping.
 *
 * The palette is what themes change. The role mapping (capture name →
 * palette token) is theme-independent: if a theme replaces "string" in
 * the palette, every role that maps to "string" follows automatically.
 *
 * Roles are looked up walk-by-dot (string.special.symbol → string.special
 * → string), so a more specific role wins when registered.
 * =================================================================== */
void ts_seed_default_theme(void) {
    /* --- Palette: bundled "default" colours, mirrors src/lib/theme.h.
     *     A theme plugin (e.g. tokyo_night) may overwrite any of these. --- */
    theme_palette_set("string", COLOR_STRING);
    theme_palette_set("comment", COLOR_COMMENT);
    theme_palette_set("variable", COLOR_VARIABLE);
    theme_palette_set("constant", COLOR_CONSTANT);
    theme_palette_set("number", COLOR_NUMBER);
    theme_palette_set("keyword", COLOR_KEYWORD);
    theme_palette_set("type", COLOR_TYPE);
    theme_palette_set("function", COLOR_FUNCTION);
    theme_palette_set("attribute", COLOR_ATTRIBUTE);
    theme_palette_set("property", COLOR_PROPERTY);
    theme_palette_set("label", COLOR_LABEL);
    theme_palette_set("parameter", COLOR_PARAMETER);
    theme_palette_set("builtin", COLOR_BUILTIN);
    theme_palette_set("operator", COLOR_OPERATOR);
    theme_palette_set("punctuation", COLOR_PUNCT);
    theme_palette_set("title", COLOR_TITLE);
    theme_palette_set("uri", COLOR_URI);
    theme_palette_set("diag.error", COLOR_DIAG_ERROR);
    theme_palette_set("diag.warn", COLOR_DIAG_WARN);
    theme_palette_set("diag.note", COLOR_DIAG_NOTE);

    /* --- Role map: capture name → palette token. Plugins can overwrite
     *     individual entries (e.g. markdown plugin re-maps text.emphasis
     *     to a raw italic SGR). --- */
    highlight_set("string", "string");
    highlight_set("escape", "string");
    highlight_set("comment", "comment");
    highlight_set("variable", "variable");
    highlight_set("variable.parameter", "parameter");
    highlight_set("parameter", "parameter");
    highlight_set("variable.builtin", "builtin");
    highlight_set("constant", "constant");
    highlight_set("constant.builtin", "builtin");
    highlight_set("boolean", "builtin");
    highlight_set("number", "number");
    highlight_set("float", "number");
    highlight_set("keyword", "keyword");
    highlight_set("conditional", "keyword");
    highlight_set("repeat", "keyword");
    highlight_set("include", "keyword");
    highlight_set("preproc", "keyword");
    highlight_set("type", "type");
    highlight_set("type.builtin", "builtin");
    highlight_set("module", "type");
    highlight_set("namespace", "type");
    highlight_set("constructor", "type");
    highlight_set("function", "function");
    highlight_set("function.builtin", "builtin");
    highlight_set("method", "function");
    highlight_set("property", "property");
    highlight_set("field", "property");
    highlight_set("attribute", "attribute");
    highlight_set("label", "label");
    highlight_set("tag", "label");
    highlight_set("operator", "operator");
    highlight_set("punctuation", "punctuation");
    highlight_set("punctuation.special", "operator");
    highlight_set("delimiter", "punctuation");
    highlight_set("text", "comment");
    highlight_set("text.title", "title");
    highlight_set("text.literal", "string");
    highlight_set("text.uri", "uri");
    highlight_set("text.reference", "type");
    highlight_set("text.danger", "diag.error");
    highlight_set("text.warning", "diag.warn");
    highlight_set("text.note", "diag.note");
    highlight_set("exception", "diag.error");
}

static const char *capture_name_to_sgr(const char *name, uint32_t nlen) {
    return highlight_lookup(name, nlen);
}

/* ===================================================================
 * HOOK_RENDER_PRE: push AttrSpans for the rows being painted.
 * =================================================================== */

/* Push spans for one (tree, query) over a byte range, splitting each
 * query capture by line boundaries. `line_starts[r]` is the chars-
 * space byte offset where row r begins. */
static void push_spans_from_tree(TSTree *tree, TSQuery *query, const char *src,
                                 size_t src_len, uint32_t range_start,
                                 uint32_t range_end, uint32_t clip_start,
                                 uint32_t clip_end, const uint32_t *line_starts,
                                 int num_rows, AttrSpans *spans) {
    if (!tree || !query)
        return;
    TSNode root = ts_tree_root_node(tree);
    TSQueryCursor *cur = ts_query_cursor_new();
    ts_query_cursor_exec(cur, query, root);
    ts_query_cursor_set_byte_range(cur, range_start, range_end);

    /* Iterate captures, not matches: next_capture yields them ordered
     * by node start byte, ties by pattern index. Pushed in that order,
     * the span tiebreak (later push wins) gives the nvim-treesitter
     * semantics every upstream query is written against — a later
     * pattern overrides an earlier one on the same node, and an inner
     * node overrides the outer one enclosing it. */
    TSQueryMatch m;
    uint32_t ci;
    while (ts_query_cursor_next_capture(cur, &m, &ci)) {
        if (!match_predicates_ok(query, &m, src, src_len))
            continue;
        {
            TSQueryCapture c = m.captures[ci];
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

            /* Walk the rows this capture intersects and push one span
             * per row's slice. Linear scan from a binary-searched
             * starting row keeps multi-line tokens cheap. */
            int row = line_of_byte(line_starts, num_rows, s);
            while (row < num_rows && line_starts[row] < e) {
                uint32_t row_start = line_starts[row];
                /* Line ends at the next line's newline, or at src end. */
                uint32_t row_end = row + 1 < num_rows ? line_starts[row + 1] - 1
                                                      : (uint32_t)src_len;
                uint32_t cs = s > row_start ? s : row_start;
                uint32_t ce = e < row_end ? e : row_end;
                if (ce > cs) {
                    attrspan_push(spans, row, (int)(cs - row_start),
                                  (int)(ce - row_start), sgr, 0);
                }
                row++;
            }
        }
    }
    ts_query_cursor_delete(cur);
}

void ts_render_pre_hook(const struct HookRenderEvent *event) {
    if (!event || !event->buf || !event->spans)
        return;
    Buffer *buf = event->buf;
    if (!g_ts_enabled)
        return;
    TSState *st = ts_state_get(buf);
    if (!st)
        return;
    /* Reparse if the buffer changed since the last frame. Replaces
     * the per-frame reparse-all loop that used to live in
     * src/terminal.c — the renderer fires this hook once per visible
     * window, so tree-sitter sees the same trigger. */
    if (st->parser && st->lang)
        ts_buffer_reparse(buf);
    if (!st->tree || !st->query || !st->line_starts)
        return;

    /* Only the rows being painted: query the byte range they cover.
     * Line offsets come from the parsed text, so they match the tree. */
    int n = st->num_lines;
    int r0 = event->row_start < n ? event->row_start : n;
    int r1 = event->row_end < n ? event->row_end : n;
    if (r0 < 0 || r1 <= r0)
        return;
    uint32_t start = st->line_starts[r0];
    uint32_t end = r1 < n ? st->line_starts[r1] : (uint32_t)st->src_len;

    push_spans_from_tree(st->tree, st->query, st->src, st->src_len, start, end,
                         start, end, st->line_starts, n, event->spans);

    /* Sub-language segments for each injection range in view. */
    for (int j = 0; j < st->num_injections; j++) {
        TSInjectionRange *ir = &st->injections[j];
        if (ir->end_byte <= start || ir->start_byte >= end)
            continue;
        TSSubLang *sub = find_sub_lang(st, ir->lang_name);
        if (!sub || !sub->tree || !sub->query)
            continue;
        uint32_t cs = ir->start_byte > start ? ir->start_byte : start;
        uint32_t ce = ir->end_byte < end ? ir->end_byte : end;
        push_spans_from_tree(sub->tree, sub->query, st->src, st->src_len, cs,
                             ce, cs, ce, st->line_starts, n, event->spans);
    }
}
