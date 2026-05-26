#include "picker.h"
#include "stb_ds.h"
#include <stdlib.h>
#include <string.h>

/* stb_ds string-keyed hashmap. Keys are heap-owned strdup'd strings;
 * `sh_new_strdup` makes hmput/shput take ownership of the duplicate. */
typedef struct { char *key; PickerFn value; } PickerEntry;
static PickerEntry *g_pickers = NULL;

static void ensure_init(void) {
    if (!g_pickers) {
        sh_new_strdup(g_pickers);
    }
}

void picker_register(const char *name, PickerFn fn) {
    if (!name || !*name) return;
    ensure_init();
    if (!fn) {
        shdel(g_pickers, name);
        return;
    }
    shput(g_pickers, name, fn);
}

PickerFn picker_get(const char *name) {
    if (!name || !*name || !g_pickers) return NULL;
    ptrdiff_t i = shgeti(g_pickers, name);
    return i < 0 ? NULL : g_pickers[i].value;
}

int picker_invoke(const char *name, const char *seed) {
    PickerFn fn = picker_get(name);
    if (!fn) return 0;
    fn(seed);
    return 1;
}
