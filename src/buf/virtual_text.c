/* Virtual text — phase 1.
 *
 * Display-only annotations attached to buffer rows. Phase 1 supports
 * only end-of-line placement: a colored suffix appended after the
 * row's last visible glyph on its last visual segment.
 *
 * Memory model: the per-buffer VtTable owns each VtMark's `text`
 * allocation. `sgr` is borrowed and expected to point at a static
 * theme literal — never freed.
 *
 * Edit invalidation: any buffer mutation drops every mark in the
 * buffer. Providers re-publish on the next refresh tick. This trades
 * a brief flicker for a trivially correct lifecycle — no line-shift
 * arithmetic, no implicit contract that every edit primitive (region
 * delete, backspace-join, macro replay, undo) fire the right hooks.
 */

#include "buf/virtual_text.h"
#include "buf/buffer.h"
#include "hooks.h"
#include "stb_ds.h"

#include <stdlib.h>
#include <string.h>

/* Namespace registry — process-wide, tiny, never removed. */
typedef struct {
    char *name;
} VtNs;

static VtNs *g_ns = NULL;

int vtext_ns_create(const char *name) {
    if (!name || !*name) return -1;
    for (ptrdiff_t i = 0; i < arrlen(g_ns); i++) {
        if (strcmp(g_ns[i].name, name) == 0) return (int)i;
    }
    VtNs ns = {.name = strdup(name)};
    if (!ns.name) return -1;
    arrput(g_ns, ns);
    return (int)arrlen(g_ns) - 1;
}

void vtext_init(Buffer *b) {
    if (!b) return;
    b->vtext.marks = NULL;
    vtext_hooks_install_once();
}

void vtext_free(Buffer *b) {
    if (!b) return;
    for (ptrdiff_t i = 0; i < arrlen(b->vtext.marks); i++) {
        sstr_free(&b->vtext.marks[i].text);
    }
    arrfree(b->vtext.marks);
    b->vtext.marks = NULL;
}

int vtext_buffer_has_marks(const Buffer *b) {
    return b && arrlen(b->vtext.marks) > 0;
}

int vtext_set_eol(Buffer *b, int ns, int line,
                  const char *text, size_t n, const char *sgr) {
    if (!b || !text || line < 0) return -1;
    VtMark m = {
        .ns_id    = ns,
        .line     = line,
        .place    = VT_PLACE_EOL,
        .text     = sstr_from(text, n),
        .sgr      = sgr,
        .priority = 0,
    };
    arrput(b->vtext.marks, m);
    return 0;
}

int vtext_clear_line(Buffer *b, int ns, int line) {
    if (!b) return -1;
    int dropped = 0;
    for (ptrdiff_t i = arrlen(b->vtext.marks) - 1; i >= 0; i--) {
        VtMark *m = &b->vtext.marks[i];
        if (m->ns_id == ns && m->line == line) {
            sstr_free(&m->text);
            arrdel(b->vtext.marks, i);
            dropped++;
        }
    }
    return dropped;
}

int vtext_clear_ns(Buffer *b, int ns) {
    if (!b) return -1;
    int dropped = 0;
    for (ptrdiff_t i = arrlen(b->vtext.marks) - 1; i >= 0; i--) {
        VtMark *m = &b->vtext.marks[i];
        if (m->ns_id == ns) {
            sstr_free(&m->text);
            arrdel(b->vtext.marks, i);
            dropped++;
        }
    }
    return dropped;
}

int vtext_clear_all(Buffer *b) {
    if (!b) return -1;
    int n = (int)arrlen(b->vtext.marks);
    for (ptrdiff_t i = 0; i < arrlen(b->vtext.marks); i++) {
        sstr_free(&b->vtext.marks[i].text);
    }
    arrsetlen(b->vtext.marks, 0);
    return n;
}

int vtext_collect_eol(const Buffer *b, int line,
                      const VtMark **out, int max) {
    if (!b || !out || max <= 0) return 0;
    /* Collect, then insertion-sort by priority ascending. N is tiny
     * (typically 0–2 per row), so insertion sort is fine. */
    int n = 0;
    for (ptrdiff_t i = 0; i < arrlen(b->vtext.marks) && n < max; i++) {
        const VtMark *m = &b->vtext.marks[i];
        if (m->place != VT_PLACE_EOL || m->line != line) continue;
        int j = n;
        while (j > 0 && out[j - 1]->priority > m->priority) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = m;
        n++;
    }
    return n;
}

/* ---- edit-time invalidation -------------------------------------- */

static void on_edit_line(const HookLineEvent *ev) {
    if (ev && ev->buf) vtext_clear_all(ev->buf);
}

static void on_edit_char(const HookCharEvent *ev) {
    if (ev && ev->buf) vtext_clear_all(ev->buf);
}

static int g_hooks_installed = 0;

void vtext_hooks_install_once(void) {
    if (g_hooks_installed) return;
    g_hooks_installed = 1;
    hook_register_line(HOOK_LINE_INSERT, -1, "*", on_edit_line);
    hook_register_line(HOOK_LINE_DELETE, -1, "*", on_edit_line);
    hook_register_char(HOOK_CHAR_INSERT, -1, "*", on_edit_char);
    hook_register_char(HOOK_CHAR_DELETE, -1, "*", on_edit_char);
}
