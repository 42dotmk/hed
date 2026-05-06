/* selectlist plugin: in-process selection picker rendered into a modal.
 *
 * Items are owned (deep-copied) by the picker. Keys j/k/<Down>/<Up> move
 * the highlight, g/G jump to ends, Enter picks, q/<Esc> cancels. The
 * pick callback receives (index, item, user) AFTER the modal is torn
 * down, so the callback may itself open a new picker. */

#include "hed.h"
#include "selectlist/selectlist.h"
#include "ui/winmodal.h"
#include "buf/buffer.h"
#include "buf/row.h"
#include <string.h>
#include <stdlib.h>

void buf_row_insert_in(Buffer *buf, int at, const char *s, size_t len);
void buf_row_del_in(Buffer *buf, int at);

static struct {
    int     active;
    Window *modal;
    int     buf_idx;
    char  **items;
    int     count;
    int     selected;
    SelectListCallback cb;
    void   *user;
} sl;

static void sl_repopulate(void) {
    if (!sl.active) return;
    Buffer *buf = &E.buffers[sl.buf_idx];
    while (buf->num_rows > 0) buf_row_del_in(buf, 0);
    for (int i = 0; i < sl.count; i++) {
        const char *prefix = (i == sl.selected) ? "> " : "  ";
        size_t plen = 2;
        size_t ilen = strlen(sl.items[i]);
        char  *line = malloc(plen + ilen + 1);
        if (!line) continue;
        memcpy(line, prefix, plen);
        memcpy(line + plen, sl.items[i], ilen);
        line[plen + ilen] = '\0';
        buf_row_insert_in(buf, i, line, plen + ilen);
        free(line);
    }
    buf->dirty    = 0;
    buf->readonly = 1;

    if (sl.modal) {
        if (sl.selected < sl.modal->row_offset)
            sl.modal->row_offset = sl.selected;
        if (sl.selected >= sl.modal->row_offset + sl.modal->height)
            sl.modal->row_offset = sl.selected - sl.modal->height + 1;
        sl.modal->cursor.y = sl.selected;
        sl.modal->cursor.x = 0;
    }
}

static void sl_close(void) {
    if (!sl.active) return;
    Window *modal   = sl.modal;
    int     buf_idx = sl.buf_idx;
    char  **items   = sl.items;
    int     count   = sl.count;
    memset(&sl, 0, sizeof(sl));
    sl.buf_idx = -1;

    if (modal) {
        winmodal_hide(modal);
        winmodal_destroy(modal);
    }
    if (buf_idx >= 0 && buf_idx < (int)arrlen(E.buffers)) {
        E.buffers[buf_idx].dirty = 0;
        buf_close(buf_idx);
    }
    for (int i = 0; i < count; i++) free(items[i]);
    free(items);
}

static void sl_keypress(HookKeyEvent *event) {
    if (!sl.active || !event) return;
    Window *modal = winmodal_current();
    if (!modal || modal != sl.modal) return;

    /* Swallow every key while our modal is open — otherwise unhandled
     * keys would fall through to the editor and edit the modal buffer
     * (the modal is the current window). */
    event->consumed = 1;

    switch (event->key) {
    case 'j':
    case KEY_ARROW_DOWN:
        if (sl.selected < sl.count - 1) sl.selected++;
        sl_repopulate();
        break;
    case 'k':
    case KEY_ARROW_UP:
        if (sl.selected > 0) sl.selected--;
        sl_repopulate();
        break;
    case 'g':
        sl.selected = 0;
        sl_repopulate();
        break;
    case 'G':
        sl.selected = sl.count - 1;
        sl_repopulate();
        break;
    case '\r':
    case '\n': {
        SelectListCallback cb   = sl.cb;
        void              *user = sl.user;
        int                idx  = sl.selected;
        char              *copy = sl.items[idx] ? strdup(sl.items[idx]) : NULL;
        sl_close();
        if (cb) cb(idx, copy ? copy : "", user);
        free(copy);
        break;
    }
    case 'q':
    case '\x1b':
        sl_close();
        break;
    }
}

static int sl_attach(Window *modal,
                     const char *const *items, int count,
                     SelectListCallback cb, void *user) {
    int buf_idx = -1;
    if (buf_new(NULL, &buf_idx) != ED_OK) {
        winmodal_destroy(modal);
        return -1;
    }
    Buffer *buf = &E.buffers[buf_idx];
    free(buf->filename); buf->filename = NULL;
    free(buf->title);    buf->title    = strdup("select");

    modal->buffer_index = buf_idx;

    sl.active   = 1;
    sl.modal    = modal;
    sl.buf_idx  = buf_idx;
    sl.selected = 0;
    sl.cb       = cb;
    sl.user     = user;
    sl.count    = count;
    sl.items    = malloc(sizeof(char *) * (size_t)count);
    if (!sl.items) {
        memset(&sl, 0, sizeof(sl));
        sl.buf_idx = -1;
        buf->dirty = 0;
        buf_close(buf_idx);
        winmodal_destroy(modal);
        return -1;
    }
    for (int i = 0; i < count; i++) sl.items[i] = strdup(items[i] ? items[i] : "");

    sl_repopulate();
    winmodal_show(modal);
    ed_set_status_message("selectlist: j/k move  Enter select  q/Esc cancel");
    return 0;
}

int selectlist_open(int x, int y, int width, int height,
                    const char *const *items, int count,
                    SelectListCallback cb, void *user) {
    if (!items || count <= 0) return -1;
    if (sl.active) sl_close();
    if (width  <= 0) width  = 30;
    if (height <= 0) height = count > 10 ? 10 : count;

    Window *modal = winmodal_create(x, y, width, height);
    if (!modal) return -1;
    return sl_attach(modal, items, count, cb, user);
}

int selectlist_open_anchored(int anchor_x, int anchor_y, int width,
                             const char *const *items, int count,
                             WModalAnchor prefer,
                             SelectListCallback cb, void *user) {
    if (!items || count <= 0) return -1;
    if (sl.active) sl_close();
    if (width <= 0) width = 30;
    int height = count > 10 ? 10 : count;

    Window *modal = winmodal_create_anchored(anchor_x, anchor_y, width, height, prefer);
    if (!modal) return -1;
    return sl_attach(modal, items, count, cb, user);
}

/* --- demo command -------------------------------------------------- */

static void on_demo_pick(int idx, const char *item, void *user) {
    (void)user;
    ed_set_status_message("selectlist: picked [%d] %s", idx, item);
}

static void cmd_selectlist_demo(const char *args) {
    (void)args;
    static const char *demo[] = {
        "alpha",   "bravo", "charlie", "delta",
        "echo",    "foxtrot", "golf",  "hotel",
    };
    int n = (int)(sizeof(demo) / sizeof(demo[0]));

    if (arrlen(E.windows) == 0) {
        ed_set_status_message("selectlist-demo: no window");
        return;
    }
    Window *cur = &E.windows[E.current_window];

    int anchor_y = (cur->cursor.y - cur->row_offset) + cur->top;
    int anchor_x = cur->left;
    Buffer *buf  = (cur->buffer_index >= 0 &&
                    cur->buffer_index < (int)arrlen(E.buffers))
                       ? &E.buffers[cur->buffer_index]
                       : NULL;
    if (buf && cur->cursor.y < buf->num_rows) {
        int rx   = buf_row_cx_to_rx(&buf->rows[cur->cursor.y], cur->cursor.x);
        anchor_x = (rx - cur->col_offset) + cur->left;
    }

    selectlist_open_anchored(anchor_x, anchor_y, 24, demo, n,
                             WMODAL_AUTO, on_demo_pick, NULL);
}

static int selectlist_init(void) {
    sl.buf_idx = -1;
    cmd("selectlist-demo", cmd_selectlist_demo,
        "show a sample SelectList anchored at the cursor");
    hook_register_key(HOOK_KEYPRESS, sl_keypress);
    return 0;
}

const Plugin plugin_selectlist = {
    .name   = "selectlist",
    .desc   = "in-process selection picker (modal)",
    .init   = selectlist_init,
    .deinit = NULL,
};
