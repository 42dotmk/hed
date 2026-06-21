/* mouse plugin: click / drag / scroll semantics on top of core's
 * HOOK_MOUSE events.
 *
 *   - left click   focuses the window under the pointer and places
 *                  the cursor (fold-, wrap- and gutter-aware via
 *                  window_screen_to_buffer)
 *   - left drag    enters visual mode anchored at the press point and
 *                  extends the selection while the button is held
 *   - wheel        scrolls the window under the pointer by 3 lines,
 *                  dragging the cursor along so the view sticks
 *   - :mouse       on|off|toggle terminal mouse reporting (off
 *                  restores terminal-native selection; Shift+drag
 *                  bypasses reporting on most terminals either way)
 *
 * Core only parses SGR sequences and forwards events here; disabling
 * this plugin (or :mouse off) leaves the editor fully keyboard-driven. */

#include "hed.h"
#include "input/input.h"
#include "input/prompt.h"
#include "utils/fold.h"

#include <string.h>

#define WHEEL_LINES 3

/* Press state carried between PRESS and the DRAG stream. */
static struct {
    int valid;      /* a left press has been seen */
    int win_id;     /* Window.id of the pressed window */
    int y, x;       /* buffer position of the press (drag anchor) */
    int dragging;   /* visual mode entered for this press */
} g_press;

static Buffer *win_buffer(const Window *win) {
    if (!win)
        return NULL;
    if (arrlen(E.buffers) > 0 && win->buffer_index >= 0 &&
        win->buffer_index < (int)arrlen(E.buffers))
        return &E.buffers[win->buffer_index];
    return NULL;
}

/* Index of the visible layout window containing terminal cell (x,y),
 * or -1 (status/message bar, window borders). Modal windows are not
 * in E.windows; mouse is ignored while a modal is up. */
static int hit_window(int x, int y) {
    for (int i = 0; i < (int)arrlen(E.windows); i++) {
        const Window *w = &E.windows[i];
        if (!w->visible)
            continue;
        if (y >= w->top && y < w->top + w->height &&
            x >= w->left && x < w->left + w->width)
            return i;
    }
    return -1;
}

static void clamp_cursor_x(Window *win, Buffer *buf) {
    if (win->cursor.y < 0)
        win->cursor.y = 0;
    if (win->cursor.y >= buf->num_rows)
        win->cursor.y = buf->num_rows - 1;
    int len = (int)buf->rows[win->cursor.y].chars.len;
    if (win->cursor.x > len)
        win->cursor.x = len;
    if (win->cursor.x < 0)
        win->cursor.x = 0;
}

static void on_press(const MouseEvent *ev) {
    int idx = hit_window(ev->x, ev->y);
    if (idx < 0)
        return;
    if (idx != E.current_window)
        windows_focus_index(idx);
    Window *win = &E.windows[idx];
    Buffer *buf = win_buffer(win);
    if (!buf)
        return;

    int y, x;
    if (!window_screen_to_buffer(win, ev->y, ev->x, &y, &x))
        return;

    /* A fresh click always collapses any existing selection. */
    if (E.mode == MODE_VISUAL || E.mode == MODE_VISUAL_LINE ||
        E.mode == MODE_VISUAL_BLOCK) {
        ed_set_mode(MODE_NORMAL);
    }
    win->sel.type = SEL_NONE;

    win->cursor.y = y;
    win->cursor.x = x;

    g_press.valid = 1;
    g_press.win_id = win->id;
    g_press.y = y;
    g_press.x = x;
    g_press.dragging = 0;
}

static void on_drag(const MouseEvent *ev) {
    if (!g_press.valid)
        return;
    Window *win = window_find_by_id(g_press.win_id);
    if (!win)
        return;
    Buffer *buf = win_buffer(win);
    if (!buf || g_press.y >= buf->num_rows)
        return;

    /* Clamp to the pressed window so dragging past an edge extends the
     * selection to the first/last visible line instead of dropping. */
    int srow = ev->y;
    int scol = ev->x;
    if (srow < win->top)
        srow = win->top;
    if (srow > win->top + win->height - 1)
        srow = win->top + win->height - 1;
    if (scol < win->left)
        scol = win->left;
    if (scol > win->left + win->width - 1)
        scol = win->left + win->width - 1;

    int y, x;
    if (!window_screen_to_buffer(win, srow, scol, &y, &x))
        return;

    if (!g_press.dragging) {
        win->sel.type = SEL_VISUAL;
        win->sel.anchor_y = g_press.y;
        win->sel.anchor_x = g_press.x;
        win->sel.anchor_rx =
            buf_row_cx_to_rx(&buf->rows[g_press.y], g_press.x);
        ed_set_mode(MODE_VISUAL);
        ed_set_status_message("-- VISUAL --");
        g_press.dragging = 1;
    }

    win->cursor.y = y;
    win->cursor.x = x;
}

static void on_wheel(const MouseEvent *ev, int dir) {
    int idx = hit_window(ev->x, ev->y);
    if (idx < 0)
        return;
    Window *win = &E.windows[idx];
    Buffer *buf = win_buffer(win);
    if (!buf || buf->num_rows == 0)
        return;

    win->row_offset += dir * WHEEL_LINES;
    if (win->row_offset < 0)
        win->row_offset = 0;
    /* Upper bound in visible lines; under wrap this under-counts
     * sublines, which only limits how far past EOF you can scroll. */
    int max_off =
        fold_get_visible_line_count(&buf->folds, buf->num_rows) - 1;
    if (max_off < 0)
        max_off = 0;
    if (win->row_offset > max_off)
        win->row_offset = max_off;

    /* Drag the cursor along: window_scroll() snaps the view back to
     * the cursor every frame, so a cursor left outside the new view
     * would undo the scroll. */
    int y, x;
    if (window_screen_to_buffer(win, win->top, win->left, &y, &x) &&
        win->cursor.y < y)
        win->cursor.y = y;
    if (window_screen_to_buffer(win, win->top + win->height - 1,
                                win->left, &y, &x) &&
        win->cursor.y > y)
        win->cursor.y = y;
    clamp_cursor_x(win, buf);
}

static void on_mouse(const MouseEvent *ev) {
    if (!ev)
        return;
    /* Modal flows (pickers, ask) and prompts are keyboard-driven. */
    if (E.modal_window && E.modal_window->visible)
        return;
    if (prompt_active())
        return;

    switch (ev->type) {
    case MOUSE_WHEEL_UP:   on_wheel(ev, -1); break;
    case MOUSE_WHEEL_DOWN: on_wheel(ev, +1); break;
    case MOUSE_PRESS:
        if (ev->button == 0)
            on_press(ev);
        break;
    case MOUSE_DRAG:
        if (ev->button == 0)
            on_drag(ev);
        break;
    case MOUSE_RELEASE:
        /* Selection survives release (vim-like); the next press or
         * any mode change collapses it. */
        g_press.dragging = 0;
        break;
    }
}

static void cmd_mouse(const char *args) {
    int on;
    if (!args || !*args || strcmp(args, "toggle") == 0)
        on = !term_mouse_get();
    else if (strcmp(args, "on") == 0)
        on = 1;
    else if (strcmp(args, "off") == 0)
        on = 0;
    else {
        ed_set_status_message("usage: mouse [on|off|toggle]");
        return;
    }
    term_mouse_set(on);
    ed_set_status_message("mouse: %s", on ? "on" : "off");
}

static int mouse_init(void) {
    hook_register_mouse(HOOK_MOUSE, on_mouse);
    cmd("mouse", cmd_mouse, "terminal mouse reporting: on|off|toggle");
    term_mouse_set(1);
    return 0;
}

static void mouse_deinit(void) {
    term_mouse_set(0);
    hook_unregister(HOOK_MOUSE, (HookFn)on_mouse);
}

const Plugin plugin_mouse = {
    .name   = "mouse",
    .desc   = "mouse support: click to place cursor, drag to select, wheel to scroll",
    .init   = mouse_init,
    .deinit = mouse_deinit,
};
