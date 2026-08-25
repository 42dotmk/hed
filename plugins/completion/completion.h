#ifndef HED_PLUGIN_COMPLETION_H
#define HED_PLUGIN_COMPLETION_H

/* completion plugin: VSCode-style autocompletion menu.
 *
 * The plugin owns the popup (a non-focus-stealing anchored modal), the
 * trigger logic (debounced auto-trigger while typing + :complete /
 * Ctrl-Space), filtering, sorting, rendering and the accept edit.
 * It knows nothing about where items come from: providers register a
 * CompletionSource and deliver items asynchronously through
 * completion_provide(). plugins/lsp registers the first source.
 */

#include "plugin.h"

typedef struct Buffer Buffer;

extern const Plugin plugin_completion;

typedef enum {
    CMP_KIND_TEXT = 0,
    CMP_KIND_FN,
    CMP_KIND_VAR,
    CMP_KIND_FIELD,
    CMP_KIND_KW,
    CMP_KIND_MOD,
    CMP_KIND_TYPE,
    CMP_KIND_SNIP,
    CMP_KIND_CONST,
    CMP_KIND_FILE,
    CMP_KIND_DIR,
    CMP_KIND_OTHER,
} CmpKind;

/* One completion candidate. All strings are owned by the item (the
 * menu frees them); insert_text/detail/sort_text may be NULL. */
typedef struct {
    char *label;       /* display + filter text */
    char *insert_text; /* NULL -> insert label */
    char *detail;      /* right column, e.g. a type signature */
    char *sort_text;   /* server-provided sort key */
    CmpKind kind;
    /* Byte replacement range on the request line ([start, end)); the
     * accepted text replaces at least [start, cursor). -1/-1 -> the
     * menu falls back to [word_start, cursor). */
    int edit_start;
    int edit_end;
} CmpItem;

typedef struct CompletionSource {
    const char *name;
    /* Can this source complete in `buf` at all (e.g. server attached)? */
    int (*available)(Buffer *buf);
    /* Extra trigger characters beyond [A-Za-z0-9_], e.g. '.' or ':'.
     * May be NULL. */
    int (*is_trigger_char)(Buffer *buf, int c);
    /* Fire an async request for position (line, col) — col is a BYTE
     * column. Deliver results with completion_provide(token, ...). */
    void (*request)(Buffer *buf, int line, int col, unsigned token);
} CompletionSource;

/* Register a source. `src` is borrowed and must have static lifetime. */
int completion_source_register(const CompletionSource *src);

/* Async delivery. The menu takes ownership of items[] (a malloc'd
 * array) and every string inside. A stale token (dismissed, retyped,
 * buffer switched) frees everything and is a no-op. items may be NULL
 * with n == 0 ("no results"). Duplicate insert texts across sources
 * are deduped, keeping the richer item (kind/detail beat plain TEXT).
 * A source may call this synchronously from request() — but note the
 * call can create the menu buffer and thus reallocate E.buffers, so
 * don't touch Buffer pointers afterwards. */
void completion_provide(unsigned token, CmpItem *items, int n);

/* :complete — open the menu at the cursor without waiting for the
 * idle trigger. Toggles the menu closed when it is already open. */
void completion_trigger_manual(void);

int completion_menu_visible(void);

#endif /* HED_PLUGIN_COMPLETION_H */
