#include "fold_methods/fold_methods.h"
#include "buf/buffer.h"
#include "hooks.h"
#include "stb_ds.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *name;
    FoldDetectFn fn;
} FoldMethodEntry;

typedef struct {
    char *filetype;
    char *method;
} FoldDefault;

static FoldMethodEntry *g_methods = NULL;
static FoldDefault    *g_defaults = NULL;

static int find_method_idx(const char *name) {
    if (!name)
        return -1;
    for (ptrdiff_t i = 0; i < arrlen(g_methods); i++) {
        if (strcmp(g_methods[i].name, name) == 0)
            return (int)i;
    }
    return -1;
}

static int find_default_idx(const char *filetype) {
    if (!filetype)
        return -1;
    for (ptrdiff_t i = 0; i < arrlen(g_defaults); i++) {
        if (strcmp(g_defaults[i].filetype, filetype) == 0)
            return (int)i;
    }
    return -1;
}

static void manual_noop(Buffer *buf) { (void)buf; }

void fold_method_register(const char *name, FoldDetectFn fn) {
    if (!name || !*name)
        return;
    FoldDetectFn use = fn ? fn : manual_noop;
    int idx = find_method_idx(name);
    if (idx >= 0) {
        g_methods[idx].fn = use;
        return;
    }
    char *copy = strdup(name);
    if (!copy)
        return;
    FoldMethodEntry e = {.name = copy, .fn = use};
    arrput(g_methods, e);
}

FoldDetectFn fold_method_lookup(const char *name) {
    int idx = find_method_idx(name);
    return idx >= 0 ? g_methods[idx].fn : NULL;
}

void fold_apply_method(Buffer *buf, const char *name) {
    if (!buf)
        return;
    FoldDetectFn fn = fold_method_lookup(name);
    if (fn)
        fn(buf);
}

void fold_method_set_default(const char *filetype, const char *method_name) {
    if (!filetype || !*filetype)
        return;
    int idx = find_default_idx(filetype);
    if (idx >= 0) {
        if (!method_name) {
            free(g_defaults[idx].filetype);
            free(g_defaults[idx].method);
            arrdel(g_defaults, idx);
            return;
        }
        char *copy = strdup(method_name);
        if (!copy)
            return;
        free(g_defaults[idx].method);
        g_defaults[idx].method = copy;
        return;
    }
    if (!method_name)
        return;
    FoldDefault d = {.filetype = strdup(filetype),
                     .method   = strdup(method_name)};
    if (!d.filetype || !d.method) {
        free(d.filetype);
        free(d.method);
        return;
    }
    arrput(g_defaults, d);
}

const char *fold_method_get_default(const char *filetype) {
    int idx = find_default_idx(filetype);
    return idx >= 0 ? g_defaults[idx].method : NULL;
}

int fold_method_count(void) { return (int)arrlen(g_methods); }

const char *fold_method_name_at(int idx) {
    if (idx < 0 || idx >= (int)arrlen(g_methods))
        return NULL;
    return g_methods[idx].name;
}

/* On buffer open, apply the filetype default if the buffer has no
 * explicit choice. Users can still :foldmethod afterwards (last-write-
 * wins). Synthetic buffers without a registered default are skipped. */
static void on_buffer_open(HookBufferEvent *event) {
    if (!event || !event->buf)
        return;
    Buffer *buf = event->buf;
    if (buf->fold_method)
        return;
    const char *def =
        fold_method_get_default(buf->filetype ? buf->filetype : "txt");
    if (!def)
        return;
    char *copy = strdup(def);
    if (!copy)
        return;
    buf->fold_method = copy;
    fold_apply_method(buf, def);
}

void fold_method_init(void) {
    fold_method_register("manual",  manual_noop);
    fold_method_register("bracket", fold_detect_brackets);
    fold_method_register("indent",  fold_detect_indent);
    hook_register_buffer(HOOK_BUFFER_OPEN, -1, "*", on_buffer_open);
}
