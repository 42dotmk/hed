/* selectlist plugin: in-process selection picker rendered into a modal.
 *
 * Items are owned (deep-copied) by the picker. Keys j/k/<Down>/<Up> move
 * the highlight, g/G jump to ends, Enter picks, q/<Esc> cancels. The
 * pick callback receives (index, item, user) AFTER the modal is torn
 * down, so the callback may itself open a new picker. */

#include "selectlist/selectlist.h"
#include "buf/buffer.h"
#include "buf/row.h"
#include "hed.h"
#include "ui/winmodal.h"
#include <stdlib.h>
#include <string.h>

static struct {
    int active;
    Window *modal;
    int buf_idx;
    char **items;
    int count;
    int selected;
    SelectListCallback cb;
    void *user;
} sl;

static void sl_repopulate(void) {
    if (!sl.active)
        return;
    Buffer *buf = &E.buffers[sl.buf_idx];
    buf_special_clear(buf);
    for (int i = 0; i < sl.count; i++)
        buf_special_addf(buf, "%s%s", (i == sl.selected) ? "> " : "  ",
                         sl.items[i]);
    buf->dirty = 0;

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
    if (!sl.active)
        return;
    Window *modal = sl.modal;
    int buf_idx = sl.buf_idx;
    char **items = sl.items;
    int count = sl.count;
    memset(&sl, 0, sizeof(sl));
    sl.buf_idx = -1;

    if (buf_idx >= 0)
        buf_special_close(buf_idx); /* tears down the modal showing it */
    else if (modal)
        winmodal_destroy(modal);
    for (int i = 0; i < count; i++)
        free(items[i]);
    free(items);
}

static void sl_keypress(HookKeyEvent *event) {
    if (!sl.active || !event)
        return;
    Window *modal = winmodal_current();
    if (!modal || modal != sl.modal)
        return;

    /* Swallow every key while our modal is open — otherwise unhandled
     * keys would fall through to the editor and edit the modal buffer
     * (the modal is the current window). */
    event->consumed = 1;

    switch (event->key) {
    case 'j':
    case KEY_ARROW_DOWN:
        if (sl.selected < sl.count - 1)
            sl.selected++;
        sl_repopulate();
        break;
    case 'k':
    case KEY_ARROW_UP:
        if (sl.selected > 0)
            sl.selected--;
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
        SelectListCallback cb = sl.cb;
        void *user = sl.user;
        int idx = sl.selected;
        char *copy = sl.items[idx] ? strdup(sl.items[idx]) : NULL;
        sl_close();
        if (cb)
            cb(idx, copy ? copy : "", user);
        free(copy);
        break;
    }
    case 'q':
    case '\x1b':
        sl_close();
        break;
    }
}

static int sl_attach(Window *modal, const char *const *items, int count,
                     SelectListCallback cb, void *user) {
    BufSpecial spec = {.title = "select", .readonly = 1};
    int buf_idx = buf_special_get(&spec, NULL);
    if (buf_idx < 0) {
        winmodal_destroy(modal);
        return -1;
    }

    modal->buffer_index = buf_idx;

    sl.active = 1;
    sl.modal = modal;
    sl.buf_idx = buf_idx;
    sl.selected = 0;
    sl.cb = cb;
    sl.user = user;
    sl.count = count;
    sl.items = malloc(sizeof(char *) * (size_t)count);
    if (!sl.items) {
        memset(&sl, 0, sizeof(sl));
        sl.buf_idx = -1;
        buf_special_close(buf_idx);
        winmodal_destroy(modal);
        return -1;
    }
    for (int i = 0; i < count; i++)
        sl.items[i] = strdup(items[i] ? items[i] : "");

    sl_repopulate();
    winmodal_show(modal);
    ed_set_status_message("selectlist: j/k move  Enter select  q/Esc cancel");
    return 0;
}

int selectlist_open(int x, int y, int width, int height,
                    const char *const *items, int count, SelectListCallback cb,
                    void *user) {
    if (!items || count <= 0)
        return -1;
    if (sl.active)
        sl_close();
    if (width <= 0)
        width = 30;
    if (height <= 0)
        height = count > 10 ? 10 : count;

    Window *modal = winmodal_create(x, y, width, height);
    if (!modal)
        return -1;
    return sl_attach(modal, items, count, cb, user);
}

int selectlist_open_anchored(int anchor_x, int anchor_y, int width,
                             const char *const *items, int count,
                             WModalAnchor prefer, SelectListCallback cb,
                             void *user) {
    if (!items || count <= 0)
        return -1;
    if (sl.active)
        sl_close();
    if (width <= 0)
        width = 30;
    int height = count > 10 ? 10 : count;

    Window *modal =
        winmodal_create_anchored(anchor_x, anchor_y, width, height, prefer);
    if (!modal)
        return -1;
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
        "alpha", "bravo",   "charlie", "delta",
        "echo",  "foxtrot", "golf",    "hotel",
    };
    int n = (int)(sizeof(demo) / sizeof(demo[0]));

    if (arrlen(E.windows) == 0) {
        ed_set_status_message("selectlist-demo: no window");
        return;
    }
    Window *cur = &E.windows[E.current_window];

    int anchor_x, anchor_y;
    win_cursor_screen_pos(cur, &anchor_x, &anchor_y);

    selectlist_open_anchored(anchor_x, anchor_y, 24, demo, n, WMODAL_AUTO,
                             on_demo_pick, NULL);
}

static int selectlist_init(void) {
    sl.buf_idx = -1;
    cmd("selectlist-demo", cmd_selectlist_demo,
        "show a sample SelectList anchored at the cursor");
    hook_register_key(HOOK_KEYPRESS, sl_keypress);
    return 0;
}

const Plugin plugin_selectlist = {
    .name = "selectlist",
    .desc = "in-process selection picker (modal)",
    .init = selectlist_init,
    .deinit = NULL,
};
