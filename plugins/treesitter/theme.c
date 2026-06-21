#include "theme.h"
#include "stb_ds.h"

typedef struct {
    char       *key;
    const char *value;
} PaletteEntry;

typedef struct {
    char          *key;
    theme_apply_fn value;
} ThemeEntry;

static PaletteEntry *g_palette = NULL;
static ThemeEntry   *g_themes  = NULL;
static int           g_palette_inited = 0;
static int           g_themes_inited = 0;
static const char   *g_active = NULL;

/* NULL-terminated theme name list, rebuilt lazily on theme_list(). */
static const char  **g_names_cache = NULL;

static void ensure_palette(void) {
    if (g_palette_inited)
        return;
    sh_new_strdup(g_palette);
    g_palette_inited = 1;
}

static void ensure_themes(void) {
    if (g_themes_inited)
        return;
    sh_new_strdup(g_themes);
    g_themes_inited = 1;
}

int theme_palette_set(const char *name, const char *sgr) {
    if (!name || !sgr)
        return -1;
    ensure_palette();
    shput(g_palette, name, sgr);
    return 0;
}

const char *theme_palette_get(const char *name) {
    if (!g_palette_inited || !name)
        return NULL;
    PaletteEntry *e = shgetp_null(g_palette, name);
    return e ? e->value : NULL;
}

int theme_register(const char *name, theme_apply_fn apply) {
    if (!name || !apply)
        return -1;
    ensure_themes();
    shput(g_themes, name, apply);
    /* Names cache is now stale. */
    if (g_names_cache) {
        arrfree(g_names_cache);
        g_names_cache = NULL;
    }
    return 0;
}

int theme_activate(const char *name) {
    if (!g_themes_inited || !name)
        return -1;
    ThemeEntry *e = shgetp_null(g_themes, name);
    if (!e)
        return -1;
    e->value();
    /* Stable pointer: stb_ds string-hash duplicates keys and never frees them
     * while the entry is live. Safe to retain across calls. */
    g_active = e->key;
    return 0;
}

const char *theme_active_name(void) { return g_active; }

const char *const *theme_list(void) {
    if (!g_themes_inited)
        return NULL;
    if (g_names_cache)
        return g_names_cache;
    int n = (int)shlen(g_themes);
    for (int i = 0; i < n; i++)
        arrput(g_names_cache, (const char *)g_themes[i].key);
    arrput(g_names_cache, NULL);
    return g_names_cache;
}
