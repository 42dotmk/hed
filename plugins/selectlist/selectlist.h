#ifndef HED_PLUGIN_SELECTLIST_H
#define HED_PLUGIN_SELECTLIST_H

#include "plugin.h"
#include "ui/winmodal.h"

extern const Plugin plugin_selectlist;

typedef void (*SelectListCallback)(int index, const char *item, void *user);
/* Fired after each pass-through edit (insert/backspace) in interactive
 * mode. Receives the new host-buffer cursor (cy=row, cx=column). */
typedef void (*SelectListChangeCallback)(int cx, int cy, void *user);

/* Optional behaviors for the picker. NULL or zeroed → default (modal,
 * non-interactive, consumes all keys, closes on Enter/Esc/q). */
typedef struct {
    /* 1 → "interactive" mode: while the modal is open, printable keys
     * and backspace flow through to the host buffer (the buffer that
     * was current when the picker opened) and `on_change` fires after
     * each edit. Up/Down/C-n/C-p still move the selection; Enter still
     * picks; Esc still cancels. Other keys close the modal and fall
     * through. */
    int                       interactive;
    SelectListChangeCallback  on_change;
} SelectListOptions;

/* Open a SelectList at a fixed position. Pass x=-1, y=-1 to center.
 * Returns 0 on success, -1 on failure. Closes any existing list first. */
int selectlist_open(int x, int y, int width, int height,
                    const char *const *items, int count,
                    SelectListCallback cb, void *user);

/* Open a SelectList anchored at a screen cell (1-based). */
int selectlist_open_anchored(int anchor_x, int anchor_y, int width,
                             const char *const *items, int count,
                             WModalAnchor prefer,
                             SelectListCallback cb, void *user);

/* Open with options (interactive + on_change). `opts` may be NULL. */
int selectlist_open_anchored_ex(int anchor_x, int anchor_y, int width,
                                const char *const *items, int count,
                                WModalAnchor prefer,
                                SelectListCallback cb, void *user,
                                const SelectListOptions *opts);

/* Replace the items shown by an open picker without closing it.
 * Selection is reset to 0. Returns 0 on success, -1 if no list is open. */
int selectlist_set_items(const char *const *items, int count);

/* True if a SelectList modal is currently open. */
int selectlist_is_active(void);

/* Programmatically close the open picker (no pick). The user callback
 * fires with index=-1 to signal a cancel, so callers can free state. */
void selectlist_close(void);

#endif /* HED_PLUGIN_SELECTLIST_H */
