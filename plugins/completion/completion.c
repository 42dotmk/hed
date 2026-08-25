/* completion plugin: VSCode-style autocompletion menu.
 *
 * A non-focus-stealing anchored modal floats under the word being
 * typed while normal insert-mode editing continues in the text window.
 * Typing identifier characters re-filters the list live; Up/Down/C-n/
 * C-p move the highlight; Tab/Enter accept; Esc dismisses and stays in
 * insert mode. The menu auto-triggers after a short idle on identifier
 * or source trigger characters, or manually via :complete (Ctrl-Space).
 *
 * Items come from registered CompletionSources (see completion.h) and
 * arrive asynchronously through completion_provide(); a generation
 * token drops responses that outlive their request.
 */

#include "completion/completion.h"
#include "hed.h"
#include "select_loop.h"
#include "treesitter/theme.h"

#define CMP_IDLE_MS 150
#define CMP_MAX_SOURCES 8
#define CMP_MAX_VISIBLE 10
#define CMP_LABEL_MAX_W 30
#define CMP_DETAIL_MAX_W 24
#define CMP_MENU_MAX_W 60
#define CMP_TIMER_NAME "completion:idle"

/* Fallback SGRs; a theme may override via the menu.* palette keys. */
#define CMP_SGR_SEL_DEFAULT "\x1b[48;5;237m"
#define CMP_SGR_KIND "\x1b[2m"

static const CompletionSource *g_sources[CMP_MAX_SOURCES];
static int g_nsources = 0;

/* Byte offsets of the styled columns within one rendered menu row. */
typedef struct {
    int kind_off;
    int detail_off;
    int detail_len;
} CmpRowMeta;

typedef struct {
    int idx; /* into M.items */
    int rank;
} CmpFiltEnt;

static struct {
    int active;           /* popup shown */
    unsigned token;       /* generation; ++ on every dismiss and request */
    int buf_idx;          /* text buffer being completed */
    int line;             /* anchor row */
    int word_start;       /* byte col of the word being completed */
    CmpItem *items;       /* stb_ds; owned */
    CmpFiltEnt *filtered; /* stb_ds; visible subset, sorted */
    CmpRowMeta *meta;     /* stb_ds; per visible row */
    int sel;              /* selected index into filtered */
    int width;            /* menu width in cells */
    int menu_buf_idx;     /* buf_special index, -1 when closed */
    Window *modal;        /* nofocus modal, NULL when closed */
    char filter[128];
} M = {.buf_idx = -1, .menu_buf_idx = -1};

/* ------------------------------------------------------------ helpers */

/* Truncate an stb_ds array without freeing its storage. arrsetlen(a,0)
 * trips -Wtype-limits (unsigned < 0 inside the macro), so delete the
 * elements instead. */
#define cmp_arr_clear(a)                                                       \
    do {                                                                       \
        if (arrlen(a))                                                         \
            arrdeln(a, 0, arrlen(a));                                          \
    } while (0)

static int cmp_is_ident(int c) {
    /* Bytes >= 128 are UTF-8 continuation/lead bytes of word-ish text. */
    return c == '_' || (c >= 0 && c < 128 && isalnum(c)) || c >= 128;
}

static int cmp_word_start_col(Buffer *buf, int line, int cx) {
    if (!buf || line < 0 || line >= buf->num_rows)
        return cx;
    const char *s = buf->rows[line].chars.data;
    int i = cx;
    while (i > 0 && cmp_is_ident((unsigned char)s[i - 1]))
        i--;
    return i;
}

static const char *cmp_kind_tag(CmpKind k) {
    switch (k) {
    case CMP_KIND_FN:
        return "fn";
    case CMP_KIND_VAR:
        return "va";
    case CMP_KIND_FIELD:
        return "fd";
    case CMP_KIND_KW:
        return "kw";
    case CMP_KIND_MOD:
        return "md";
    case CMP_KIND_TYPE:
        return "ty";
    case CMP_KIND_SNIP:
        return "sn";
    case CMP_KIND_CONST:
        return "cn";
    case CMP_KIND_TEXT:
        return "tx";
    default:
        return "  ";
    }
}

/* Palette lookups guarded weakly: without the treesitter plugin the
 * theme registry is absent and the hard-coded defaults apply. */
static const char *cmp_sgr(const char *key, const char *fallback) {
    if (&theme_palette_get) {
        const char *s = theme_palette_get(key);
        if (s)
            return s;
    }
    return fallback;
}

static void cmp_items_clear(void) {
    for (ptrdiff_t i = 0; i < arrlen(M.items); i++) {
        free(M.items[i].label);
        free(M.items[i].insert_text);
        free(M.items[i].detail);
        free(M.items[i].sort_text);
    }
    cmp_arr_clear(M.items);
    cmp_arr_clear(M.filtered);
    cmp_arr_clear(M.meta);
    M.sel = 0;
    M.filter[0] = '\0';
}

static void cmp_menu_dismiss(void) {
    M.token++; /* drop in-flight responses */
    M.active = 0;
    Window *modal = M.modal;
    M.modal = NULL;
    int menu_idx = M.menu_buf_idx;
    M.menu_buf_idx = -1;
    cmp_items_clear();
    M.buf_idx = -1;
    if (modal)
        winmodal_destroy(modal);
    if (menu_idx >= 0)
        buf_special_close(menu_idx);
}

int completion_menu_visible(void) { return M.active; }

/* ------------------------------------------------------- filter / sort */

static int cmp_rank_one(const char *label, const char *f, size_t flen) {
    if (flen == 0)
        return 0;
    if (strncmp(label, f, flen) == 0)
        return 0;
    if (strncasecmp(label, f, flen) == 0)
        return 1;
    /* case-insensitive subsequence */
    size_t i = 0;
    for (const char *l = label; *l && i < flen; l++) {
        if (tolower((unsigned char)*l) == tolower((unsigned char)f[i]))
            i++;
    }
    return i == flen ? 2 : -1;
}

static int cmp_filt_cmp(const void *a, const void *b) {
    const CmpFiltEnt *x = a, *y = b;
    if (x->rank != y->rank)
        return x->rank - y->rank;
    const CmpItem *ix = &M.items[x->idx], *iy = &M.items[y->idx];
    const char *sx = ix->sort_text ? ix->sort_text : ix->label;
    const char *sy = iy->sort_text ? iy->sort_text : iy->label;
    int c = strcmp(sx, sy);
    if (c)
        return c;
    return x->idx - y->idx;
}

/* ------------------------------------------------------------- render */

static void cmp_menu_follow_sel(void) {
    if (!M.modal)
        return;
    if (M.sel < M.modal->row_offset)
        M.modal->row_offset = M.sel;
    if (M.sel >= M.modal->row_offset + M.modal->height)
        M.modal->row_offset = M.sel - M.modal->height + 1;
}

/* Append `s` truncated to `max_cols` display columns, then pad with
 * spaces to exactly `max_cols`. Returns bytes appended. */
static int cmp_append_col(StrBuf *sb, const char *s, int max_cols) {
    size_t before = sb->len;
    if (s && *s) {
        int start, blen;
        utf8_slice_by_columns(s, strlen(s), 0, max_cols, &start, &blen);
        strbuf_append(sb, s + start, (size_t)blen);
        int used = utf8_display_width(s + start, (size_t)blen);
        for (int i = used; i < max_cols; i++)
            strbuf_append_char(sb, ' ');
    } else {
        for (int i = 0; i < max_cols; i++)
            strbuf_append_char(sb, ' ');
    }
    return (int)(sb->len - before);
}

/* Rebuild the menu buffer rows + modal geometry from M.filtered. The
 * caller has validated buffer/window/mode. */
static void cmp_menu_show(void) {
    if (M.menu_buf_idx < 0) {
        BufSpecial spec = {.name = "[completion]",
                           .filetype = "completion-menu",
                           .readonly = 1};
        M.menu_buf_idx = buf_special_get(&spec, NULL);
        if (M.menu_buf_idx < 0) {
            cmp_menu_dismiss();
            return;
        }
    }

    int n = (int)arrlen(M.filtered);
    int label_w = 8, detail_w = 0;
    for (int i = 0; i < n; i++) {
        const CmpItem *it = &M.items[M.filtered[i].idx];
        int lw = utf8_display_width(it->label, strlen(it->label));
        if (lw > label_w)
            label_w = lw;
        if (it->detail) {
            int dw = utf8_display_width(it->detail, strlen(it->detail));
            if (dw > detail_w)
                detail_w = dw;
        }
    }
    if (label_w > CMP_LABEL_MAX_W)
        label_w = CMP_LABEL_MAX_W;
    if (detail_w > CMP_DETAIL_MAX_W)
        detail_w = CMP_DETAIL_MAX_W;
    /* " label  kd  detail " */
    int width = 1 + label_w + 2 + 2 + (detail_w ? 2 + detail_w : 0) + 1;
    while (width > CMP_MENU_MAX_W && detail_w > 0) {
        detail_w--;
        width--;
    }
    if (width > CMP_MENU_MAX_W)
        width = CMP_MENU_MAX_W;
    if (width > E.screen_cols - 2)
        width = E.screen_cols - 2;
    M.width = width;

    Buffer *mb = &E.buffers[M.menu_buf_idx];
    buf_special_clear(mb);
    cmp_arr_clear(M.meta);
    for (int i = 0; i < n; i++) {
        const CmpItem *it = &M.items[M.filtered[i].idx];
        CmpRowMeta meta = {0};
        StrBuf sb = strbuf_new();
        strbuf_append_char(&sb, ' ');
        cmp_append_col(&sb, it->label, label_w);
        strbuf_append(&sb, "  ", 2);
        meta.kind_off = (int)sb.len;
        strbuf_append(&sb, cmp_kind_tag(it->kind), 2);
        if (detail_w) {
            strbuf_append(&sb, "  ", 2);
            meta.detail_off = (int)sb.len;
            int dbytes = cmp_append_col(&sb, it->detail, detail_w);
            meta.detail_len = it->detail ? dbytes : 0;
        }
        strbuf_append_char(&sb, ' ');
        buf_special_add(mb, sb.data, sb.len);
        strbuf_free(&sb);
        arrput(M.meta, meta);
    }
    mb->dirty = 0;

    Window *win = window_cur();
    int ax, ay;
    win_text_screen_pos(win, M.line, M.word_start, &ax, &ay);
    int x = ax - 1; /* align labels under the word (1-cell left pad) */
    if (x < 1)
        x = 1;
    int h = n > CMP_MAX_VISIBLE ? CMP_MAX_VISIBLE : n;

    if (M.modal) {
        winmodal_destroy(M.modal);
        M.modal = NULL;
    }
    Window *modal = winmodal_create_anchored(x, ay, M.width, h, WMODAL_AUTO);
    if (!modal) {
        cmp_menu_dismiss();
        return;
    }
    modal->buffer_index = M.menu_buf_idx;
    modal->gutter_mode = 2; /* fixed-width gutter of 0 = no line numbers */
    modal->gutter_fixed_width = 0;
    winmodal_show_nofocus(modal);
    M.modal = modal;
    M.active = 1;
    cmp_menu_follow_sel();
}

/* Selected-row bar + column colors, pushed as AttrSpans on the menu
 * buffer. The background SGR terminates cleanly at span end because
 * the renderer's soft reset includes 49. */
static void cmp_on_render(const HookRenderEvent *ev) {
    if (!M.active || M.menu_buf_idx < 0 || !ev || !ev->buf)
        return;
    if (M.menu_buf_idx >= (int)arrlen(E.buffers) ||
        ev->buf != &E.buffers[M.menu_buf_idx])
        return;
    int n = (int)arrlen(M.meta);
    for (int r = ev->row_start; r < ev->row_end && r < n; r++) {
        const CmpRowMeta *m = &M.meta[r];
        attrspan_push(ev->spans, r, m->kind_off, m->kind_off + 2,
                      cmp_sgr("menu.kind", CMP_SGR_KIND), 10);
        if (m->detail_len > 0)
            attrspan_push(ev->spans, r, m->detail_off,
                          m->detail_off + m->detail_len,
                          cmp_sgr("menu.detail", COLOR_COMMENT), 10);
    }
    if (M.sel >= ev->row_start && M.sel < ev->row_end &&
        M.sel < ev->buf->num_rows)
        attrspan_push(ev->spans, M.sel, 0, (int)ev->buf->rows[M.sel].chars.len,
                      cmp_sgr("menu.sel", CMP_SGR_SEL_DEFAULT), 200);
}

/* ------------------------------------------------------ filter + show */

/* Re-read the live filter word, re-rank, and show/refresh the popup.
 * Dismisses when the context is gone (mode/buffer/line changed, word
 * abandoned, no matches). */
static void cmp_menu_refilter(void) {
    if (M.buf_idx < 0)
        return;
    if (arrlen(M.items) == 0) {
        if (M.active)
            cmp_menu_dismiss();
        return;
    }
    if (E.mode != MODE_INSERT || M.buf_idx >= (int)arrlen(E.buffers)) {
        cmp_menu_dismiss();
        return;
    }
    Buffer *buf = &E.buffers[M.buf_idx];
    Window *win = window_cur();
    if (!win || win->is_modal || win->buffer_index != M.buf_idx ||
        win->cursor.y != M.line || M.line >= buf->num_rows) {
        cmp_menu_dismiss();
        return;
    }
    Row *row = &buf->rows[M.line];
    int cur_x = win->cursor.x;
    if (cur_x < M.word_start || (size_t)cur_x > row->chars.len) {
        cmp_menu_dismiss();
        return;
    }
    size_t flen = (size_t)(cur_x - M.word_start);
    if (flen >= sizeof(M.filter))
        flen = sizeof(M.filter) - 1;
    memcpy(M.filter, row->chars.data + M.word_start, flen);
    M.filter[flen] = '\0';

    cmp_arr_clear(M.filtered);
    for (ptrdiff_t i = 0; i < arrlen(M.items); i++) {
        int rank = cmp_rank_one(M.items[i].label, M.filter, flen);
        if (rank >= 0) {
            CmpFiltEnt e = {.idx = (int)i, .rank = rank};
            arrput(M.filtered, e);
        }
    }
    if (arrlen(M.filtered) == 0) {
        cmp_menu_dismiss();
        return;
    }
    qsort(M.filtered, (size_t)arrlen(M.filtered), sizeof(CmpFiltEnt),
          cmp_filt_cmp);
    M.sel = 0;
    cmp_menu_show();
}

/* --------------------------------------------------- request lifecycle */

static unsigned cmp_menu_begin(int buf_idx, int line, int word_start) {
    if (M.active)
        cmp_menu_dismiss();
    M.token++;
    cmp_items_clear();
    M.buf_idx = buf_idx;
    M.line = line;
    M.word_start = word_start;
    return M.token;
}

void completion_provide(unsigned token, CmpItem *items, int n) {
    if (token != M.token || M.buf_idx < 0) {
        for (int i = 0; i < n; i++) {
            free(items[i].label);
            free(items[i].insert_text);
            free(items[i].detail);
            free(items[i].sort_text);
        }
        free(items);
        return;
    }
    for (int i = 0; i < n; i++)
        arrput(M.items, items[i]);
    free(items);
    cmp_menu_refilter();
}

int completion_source_register(const CompletionSource *src) {
    if (!src || g_nsources >= CMP_MAX_SOURCES)
        return -1;
    g_sources[g_nsources++] = src;
    return 0;
}

/* Fire a request at the current cursor. No-op unless in insert mode
 * with at least one available source. */
static void cmp_request_now(void *ud) {
    (void)ud;
    if (E.mode != MODE_INSERT)
        return;
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!buf || !win || win->is_modal)
        return;
    int buf_idx = (int)(buf - E.buffers);
    if (win->buffer_index != buf_idx)
        return;
    int line = win->cursor.y;
    int cx = win->cursor.x;
    if (line < 0 || line >= buf->num_rows)
        return;

    int any = 0;
    for (int i = 0; i < g_nsources; i++)
        if (g_sources[i]->available && g_sources[i]->available(buf))
            any = 1;
    if (!any)
        return;

    int ws = cmp_word_start_col(buf, line, cx);
    unsigned token = cmp_menu_begin(buf_idx, line, ws);
    for (int i = 0; i < g_nsources; i++)
        if (g_sources[i]->available && g_sources[i]->available(buf))
            g_sources[i]->request(buf, line, cx, token);
}

static void cmp_schedule(void) {
    ed_loop_timer_after(CMP_TIMER_NAME, CMP_IDLE_MS, cmp_request_now, NULL);
}

void completion_trigger_manual(void) {
    if (M.active) {
        cmp_menu_dismiss();
        return;
    }
    ed_loop_timer_cancel(CMP_TIMER_NAME);
    cmp_request_now(NULL);
}

/* -------------------------------------------------------------- accept */

static void cmp_menu_accept(void) {
    if (!M.active || arrlen(M.filtered) == 0 || M.sel < 0 ||
        M.sel >= (int)arrlen(M.filtered)) {
        cmp_menu_dismiss();
        return;
    }
    const CmpItem *it = &M.items[M.filtered[M.sel].idx];
    if (M.buf_idx < 0 || M.buf_idx >= (int)arrlen(E.buffers)) {
        cmp_menu_dismiss();
        return;
    }
    Buffer *buf = &E.buffers[M.buf_idx];
    Window *win = window_cur();
    if (M.line < 0 || M.line >= buf->num_rows || !win || win->is_modal ||
        win->buffer_index != M.buf_idx) {
        cmp_menu_dismiss();
        return;
    }
    Row *row = &buf->rows[M.line];

    int start = it->edit_start >= 0 ? it->edit_start : M.word_start;
    int end = win->cursor.x;
    if (it->edit_end > end)
        end = it->edit_end; /* server range may replace a suffix too */
    if (start < 0)
        start = 0;
    if (end > (int)row->chars.len)
        end = (int)row->chars.len;
    if (start > end)
        start = end;

    const char *ins = it->insert_text ? it->insert_text : it->label;
    size_t ilen = strcspn(ins, "\n"); /* v1: single-line inserts */

    /* Records into the open "insert" undo group, so leaving insert mode
     * makes the typed text + completion one undo step. */
    undo_record_replace(buf, M.line);
    size_t tail = row->chars.len - (size_t)end;
    StrBuf fresh = strbuf_new();
    strbuf_reserve(&fresh, (size_t)start + ilen + tail);
    strbuf_append(&fresh, row->chars.data, (size_t)start);
    strbuf_append(&fresh, ins, ilen);
    strbuf_append(&fresh, row->chars.data + end, tail);
    strbuf_free(&row->chars);
    row->chars = fresh;
    buf_row_update(row);
    buf->dirty++;

    int newx = start + (int)ilen;
    if (buf->cursor) {
        buf->cursor->y = M.line;
        buf->cursor->x = newx;
    }
    win->cursor.y = M.line;
    win->cursor.x = newx;

    cmp_menu_dismiss();
}

/* --------------------------------------------------------------- hooks */

static void cmp_on_char_insert(const HookCharEvent *ev) {
    if (!ev || !ev->buf || undo_is_applying(ev->buf))
        return;
    int c = ev->c;
    int ident = cmp_is_ident(c);
    int trigger = 0;
    for (int i = 0; i < g_nsources && !trigger; i++)
        if (g_sources[i]->is_trigger_char && g_sources[i]->available &&
            g_sources[i]->available(ev->buf) &&
            g_sources[i]->is_trigger_char(ev->buf, c))
            trigger = 1;

    if (M.active) {
        if (M.buf_idx < 0 || M.buf_idx >= (int)arrlen(E.buffers) ||
            ev->buf != &E.buffers[M.buf_idx] || ev->row != M.line) {
            cmp_menu_dismiss();
            return;
        }
        if (ident) {
            cmp_menu_refilter();
            return;
        }
        cmp_menu_dismiss();
        if (trigger)
            cmp_schedule();
        return;
    }
    if (ident || trigger)
        cmp_schedule();
    else
        ed_loop_timer_cancel(CMP_TIMER_NAME);
}

static void cmp_on_char_delete(const HookCharEvent *ev) {
    if (!ev || !ev->buf || undo_is_applying(ev->buf))
        return;
    if (!M.active) {
        ed_loop_timer_cancel(CMP_TIMER_NAME);
        return;
    }
    if (M.buf_idx < 0 || M.buf_idx >= (int)arrlen(E.buffers) ||
        ev->buf != &E.buffers[M.buf_idx] || ev->row != M.line) {
        cmp_menu_dismiss();
        return;
    }
    Window *win = window_cur();
    if (!win || win->is_modal || win->cursor.x < M.word_start)
        cmp_menu_dismiss();
    else
        cmp_menu_refilter();
}

static void cmp_on_mode_change(const HookModeEvent *ev) {
    if (!ev || ev->new_mode == MODE_INSERT)
        return;
    ed_loop_timer_cancel(CMP_TIMER_NAME);
    if (M.active)
        cmp_menu_dismiss();
}

static void cmp_on_cursor_move(const HookCursorEvent *ev) {
    if (!M.active || !ev)
        return;
    if (M.buf_idx < 0 || M.buf_idx >= (int)arrlen(E.buffers) ||
        ev->buf != &E.buffers[M.buf_idx] || ev->new_y != M.line ||
        ev->new_x < M.word_start)
        cmp_menu_dismiss();
}

static void cmp_on_buffer_change(HookBufferEvent *ev) {
    (void)ev;
    if (M.active)
        cmp_menu_dismiss();
}

static void cmp_on_key(HookKeyEvent *ev) {
    if (!M.active || !ev || !E.modal_window || E.modal_window != M.modal)
        return;
    switch (ev->key) {
    case KEY_ARROW_DOWN:
    case 14: /* C-n */
        M.sel = (M.sel + 1) % (int)arrlen(M.filtered);
        cmp_menu_follow_sel();
        ev->consumed = 1;
        break;
    case KEY_ARROW_UP:
    case 16: /* C-p */
        M.sel = (M.sel + (int)arrlen(M.filtered) - 1) % (int)arrlen(M.filtered);
        cmp_menu_follow_sel();
        ev->consumed = 1;
        break;
    case KEY_BTAB:
        M.sel = (M.sel + (int)arrlen(M.filtered) - 1) % (int)arrlen(M.filtered);
        cmp_menu_follow_sel();
        ev->consumed = 1;
        break;
    case '\t':
    case '\r':
    case '\n':
        cmp_menu_accept();
        ev->consumed = 1;
        break;
    case '\x1b': /* dismiss, stay in insert mode (dispatch is skipped) */
    case 0:      /* C-Space toggles off */
        cmp_menu_dismiss();
        ev->consumed = 1;
        break;
    default:
        break; /* everything else falls through to normal editing */
    }
}

/* ------------------------------------------------------------- plugin */

static void cmd_complete(const char *args) {
    (void)args;
    completion_trigger_manual();
}

static int completion_init(void) {
    cmd("complete", cmd_complete, "completion menu (manual trigger)");
    /* Ctrl+Space: terminals send \x00; key_to_string encodes "<0>". */
    cmapi("<0>", "complete", "completion menu");

    hook_register_char(HOOK_CHAR_INSERT, MODE_INSERT, "*", cmp_on_char_insert);
    hook_register_char(HOOK_CHAR_DELETE, MODE_INSERT, "*", cmp_on_char_delete);
    hook_register_mode(HOOK_MODE_CHANGE, cmp_on_mode_change);
    hook_register_cursor(HOOK_CURSOR_MOVE, -1, "*", cmp_on_cursor_move);
    hook_register_buffer(HOOK_BUFFER_SWITCH, -1, "*", cmp_on_buffer_change);
    hook_register_buffer(HOOK_BUFFER_CLOSE, -1, "*", cmp_on_buffer_change);
    hook_register_key(HOOK_KEYPRESS, cmp_on_key);
    hook_register_render(HOOK_RENDER_PRE, -1, "completion-menu", cmp_on_render);
    return 0;
}

const Plugin plugin_completion = {
    .name = "completion",
    .desc = "autocompletion menu (source-driven popup)",
    .init = completion_init,
    .deinit = NULL,
};
