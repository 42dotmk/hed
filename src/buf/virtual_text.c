#include "buf/virtual_text.h"
#include "buf/buffer.h"
#include "hooks.h"
#include "lib/vector.h"
#include "stb_ds.h"

#include <stdlib.h>
#include <string.h>

/* Namespace registry�� process-wide, tiny, never removed. */
typedef struct {
    char *name;
    int   auto_clear;   /* 1 = clear marks on edit (default); 0 = persist */
} VtNs;

static VtNs *g_ns = NULL;

int vtext_ns_create(const char *name) {
    if (!name || !*name) return -1;
    for (ptrdiff_t i = 0; i < arrlen(g_ns); i++) {
        if (strcmp(g_ns[i].name, name) == 0) return (int)i;
    }
    VtNs ns = {.name = strdup(name), .auto_clear = 1};
    if (!ns.name) return -1;
    arrput(g_ns, ns);
    return (int)arrlen(g_ns) - 1;
}

int vtext_ns_set_auto_clear(int ns, int auto_clear) {
    if (ns < 0 || ns >= (int)arrlen(g_ns)) return -1;
    g_ns[ns].auto_clear = auto_clear ? 1 : 0;
    return 0;
}

static int vtext_ns_auto_clear(int ns) {
    if (ns < 0 || ns >= (int)arrlen(g_ns)) return 1;
    return g_ns[ns].auto_clear;
}

/* Drop every mark whose namespace has auto_clear=1. Marks owned by
 * persistent namespaces (e.g. copilot ghost text) survive. */
static int vtext_clear_auto(Buffer *b) {
    if (!b) return 0;
    int dropped = 0;
    for (ptrdiff_t i = arrlen(b->vtext.marks) - 1; i >= 0; i--) {
        VtMark *m = &b->vtext.marks[i];
        if (vtext_ns_auto_clear(m->ns_id)) {
            sstr_free(&m->text);
            arrdel(b->vtext.marks, i);
            dropped++;
        }
    }
    return dropped;
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
    arr_reset(b->vtext.marks);
    return n;
}

int vtext_set_block_below(Buffer *b, int ns, int line,
                          const char *text, size_t n, const char *sgr) {
    if (!b || !text || line < 0) return -1;
    VtMark m = {
        .ns_id    = ns,
        .line     = line,
        .place    = VT_PLACE_BLOCK_BELOW,
        .text     = sstr_from(text, n),
        .sgr      = sgr,
        .priority = 0,
    };
    arrput(b->vtext.marks, m);
    return 0;
}

/* Count virtual rows produced by one block_below mark: newlines + 1. */
static int block_below_rows_in_mark(const VtMark *m) {
    int rows = 1;
    for (size_t i = 0; i < m->text.len; i++) {
        if (m->text.data[i] == '\n') rows++;
    }
    return rows;
}

int vtext_block_below_count(const Buffer *b, int line) {
    if (!b) return 0;
    int total = 0;
    for (ptrdiff_t i = 0; i < arrlen(b->vtext.marks); i++) {
        const VtMark *m = &b->vtext.marks[i];
        if (m->place == VT_PLACE_BLOCK_BELOW && m->line == line)
            total += block_below_rows_in_mark(m);
    }
    return total;
}

int vtext_block_below_at(const Buffer *b, int line, int row_index,
                         const char **out_text, size_t *out_len,
                         const char **out_sgr) {
    if (!b || row_index < 0 || !out_text || !out_len) return 0;
    for (ptrdiff_t i = 0; i < arrlen(b->vtext.marks); i++) {
        const VtMark *m = &b->vtext.marks[i];
        if (m->place != VT_PLACE_BLOCK_BELOW || m->line != line) continue;
        int rows = block_below_rows_in_mark(m);
        if (row_index >= rows) {
            row_index -= rows;
            continue;
        }
        /* Walk to the row_index-th '\n'-separated segment. */
        size_t start = 0, seg = 0;
        for (size_t j = 0; j <= m->text.len; j++) {
            if (j == m->text.len || m->text.data[j] == '\n') {
                if ((int)seg == row_index) {
                    *out_text = m->text.data + start;
                    *out_len  = j - start;
                    if (out_sgr) *out_sgr = m->sgr;
                    return 1;
                }
                seg++;
                start = j + 1;
            }
        }
        return 0;
    }
    return 0;
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
    if (ev && ev->buf) vtext_clear_auto(ev->buf);
}

static void on_edit_char(const HookCharEvent *ev) {
    if (ev && ev->buf) vtext_clear_auto(ev->buf);
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
