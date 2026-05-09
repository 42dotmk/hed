#include "highlight.h"
#include "theme.h"
#include "stb_ds.h"
#include <string.h>

typedef struct {
    char       *key;
    const char *value;
} HEntry;

static HEntry *g_table = NULL;
static int     g_inited = 0;

static void ensure_inited(void) {
    if (g_inited)
        return;
    sh_new_strdup(g_table);
    g_inited = 1;
}

int highlight_set(const char *role, const char *value) {
    if (!role || !value)
        return -1;
    ensure_inited();
    shput(g_table, role, value);
    return 0;
}

/* If value starts with ESC it's a literal SGR; otherwise resolve through the
 * palette. Palette miss → NULL (uncoloured), same as a missing role. */
static const char *resolve_value(const char *value) {
    if (!value)
        return NULL;
    if (value[0] == '\x1b')
        return value;
    return theme_palette_get(value);
}

/* Walk the dotted name from longest to shortest, returning the first hit.
 * Bounded by HL_MAX so a maliciously long capture name can't overflow the
 * stack buffer; tree-sitter capture names in practice are well under 64. */
const char *highlight_lookup(const char *capture, uint32_t len) {
    if (!g_inited || !capture || len == 0)
        return NULL;

    enum { HL_MAX = 128 };
    char buf[HL_MAX];
    if (len >= HL_MAX)
        len = HL_MAX - 1;
    memcpy(buf, capture, len);
    buf[len] = '\0';

    for (;;) {
        HEntry *e = shgetp_null(g_table, buf);
        if (e)
            return resolve_value(e->value);
        char *dot = strrchr(buf, '.');
        if (!dot)
            return NULL;
        *dot = '\0';
    }
}
